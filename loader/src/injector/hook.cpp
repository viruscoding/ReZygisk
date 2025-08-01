#include <sys/mount.h>
#include <dlfcn.h>
#include <regex.h>
#include <bitset>
#include <list>
#include <map>
#include <array>
#include <vector>

#include <lsplt.h>

#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <unistd.h>
#include <pthread.h>

#include "daemon.h"
#include "zygisk.hpp"
#include "module.hpp"
#include "misc.h"

#include "solist.h"

#include "art_method.hpp"

using namespace std;

static void hook_unloader();
static void unhook_functions();

namespace {

enum {
    POST_SPECIALIZE,
    APP_FORK_AND_SPECIALIZE,
    APP_SPECIALIZE,
    SERVER_FORK_AND_SPECIALIZE,
    DO_REVERT_UNMOUNT,
    SKIP_FD_SANITIZATION,

    FLAG_MAX
};

#define DCL_PRE_POST(name) \
void name##_pre();         \
void name##_post();

#define MAX_FD_SIZE 1024

struct ZygiskContext;

// Current context
ZygiskContext *g_ctx;

struct ZygiskContext {
    JNIEnv *env;
    union {
        void *ptr;
        AppSpecializeArgs_v5 *app;
        ServerSpecializeArgs_v1 *server;
    } args;

    const char *process;
    list<ZygiskModule> modules;

    int pid;
    bitset<FLAG_MAX> flags;
    uint32_t info_flags;
    bitset<MAX_FD_SIZE> allowed_fds;
    vector<int> exempted_fds;

    struct RegisterInfo {
        regex_t regex;
        string symbol;
        void *callback;
        void **backup;
    };

    struct IgnoreInfo {
        regex_t regex;
        string symbol;
    };

    pthread_mutex_t hook_info_lock;
    vector<RegisterInfo> register_info;
    vector<IgnoreInfo> ignore_info;

    ZygiskContext(JNIEnv *env, void *args) :
    env(env), args{args}, process(nullptr), pid(-1), info_flags(0),
    hook_info_lock(PTHREAD_MUTEX_INITIALIZER) {
        g_ctx = this;
    }
    ~ZygiskContext();

    /* Zygisksu changed: Load module fds */
    bool load_modules_only();
    void run_modules_pre();
    void run_modules_post();
    DCL_PRE_POST(fork)
    DCL_PRE_POST(app_specialize)
    DCL_PRE_POST(nativeForkAndSpecialize)
    DCL_PRE_POST(nativeSpecializeAppProcess)
    DCL_PRE_POST(nativeForkSystemServer)

    void sanitize_fds();
    bool exempt_fd(int fd);
    bool is_child() const { return pid <= 0; }

    // Compatibility shim
    void plt_hook_register(const char *regex, const char *symbol, void *fn, void **backup);
    void plt_hook_exclude(const char *regex, const char *symbol);
    void plt_hook_process_regex();

    bool plt_hook_commit();
};

#undef DCL_PRE_POST

// Global variables
vector<tuple<dev_t, ino_t, const char *, void **>> *plt_hook_list;
map<string, vector<JNINativeMethod>> *jni_hook_list;
bool should_unmap_zygisk = false;
bool enable_unloader = false;
bool hooked_unloader = false;

} // namespace

namespace {

#define DCL_HOOK_FUNC(ret, func, ...) \
ret (*old_##func)(__VA_ARGS__);       \
ret new_##func(__VA_ARGS__)

/* INFO: ReZygisk already performs a fork in ZygiskContext::fork_pre, because of that,
           we avoid duplicate fork in nativeForkAndSpecialize and nativeForkSystemServer
           by caching the pid in fork_pre function and only performing fork if the pid
           is non-0, or in other words, if we (libzygisk.so) already forked. */
DCL_HOOK_FUNC(int, fork) {
    return (g_ctx && g_ctx->pid >= 0) ? g_ctx->pid : old_fork();
}

bool update_mnt_ns(enum mount_namespace_state mns_state, bool dry_run) {
    char ns_path[PATH_MAX];
    if (rezygiskd_update_mns(mns_state, ns_path, sizeof(ns_path)) == false) {
        PLOGE("Failed to update mount namespace");

        return false;
    }

    if (dry_run) return true;

    int updated_ns = open(ns_path, O_RDONLY);
    if (updated_ns == -1) {
        PLOGE("Failed to open mount namespace [%s]", ns_path);

        return false;
    }

    const char *mns_state_str = NULL;
    if (mns_state == Clean) mns_state_str = "clean";
    else if (mns_state == Mounted) mns_state_str = "mounted";
    else mns_state_str = "unknown";

    LOGD("set mount namespace to [%s] fd=[%d]: %s", ns_path, updated_ns, mns_state_str);
    if (setns(updated_ns, CLONE_NEWNS) == -1) {
        PLOGE("Failed to set mount namespace [%s]", ns_path);
        close(updated_ns);

        return false;
    }

    close(updated_ns);

    return true;
}
struct FileDescriptorInfo {
    const int fd;
    const struct stat stat;
    const std::string file_path;
    const int open_flags;
    const int fd_flags;
    const int fs_flags;
    const off_t offset;
    const bool is_sock;
};

/* INFO: This hook avoids that umounted overlays made by root modules lead to Zygote
           to Abort its operation as it cannot open anymore.

   SOURCES:
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/fd_utils.cpp#346
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/fd_utils.cpp#544
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/com_android_internal_os_Zygote.cpp#2329
*/
DCL_HOOK_FUNC(void, _ZNK18FileDescriptorInfo14ReopenOrDetachERKNSt3__18functionIFvNS0_12basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEEEE, void *_this, void *fail_fn) {
    const int fd = *(const int *)((uintptr_t)_this + offsetof(FileDescriptorInfo, fd));
    const std::string *file_path = (const std::string *)((uintptr_t)_this + offsetof(FileDescriptorInfo, file_path));
    const int open_flags = *(const int *)((uintptr_t)_this + offsetof(FileDescriptorInfo, open_flags));
    const bool is_sock = *(const bool *)((uintptr_t)_this + offsetof(FileDescriptorInfo, is_sock));

    int new_fd;

    if (is_sock)
        goto bypass_fd_check;

    if (strncmp(file_path->c_str(), "/memfd:/boot-image-methods.art", strlen("/memfd:/boot-image-methods.art")) == 0)
        goto bypass_fd_check;

    new_fd = TEMP_FAILURE_RETRY(open(file_path->c_str(), open_flags));
    close(new_fd);
    if (new_fd == -1) {
        LOGD("Failed to open file %s, detaching it", file_path->c_str());

        close(fd);

        return;
    }

    bypass_fd_check:
        old__ZNK18FileDescriptorInfo14ReopenOrDetachERKNSt3__18functionIFvNS0_12basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEEEE(_this, fail_fn);
}

// We cannot directly call `dlclose` to unload ourselves, otherwise when `dlclose` returns,
// it will return to our code which has been unmapped, causing segmentation fault.
// Instead, we hook `pthread_attr_setstacksize` which will be called when VM daemon threads start.
DCL_HOOK_FUNC(int, pthread_attr_setstacksize, void *target, size_t size) {
    int res = old_pthread_attr_setstacksize((pthread_attr_t *)target, size);
    LOGV("Call pthread_attr_setstacksize in [tid, pid]: %d, %d", gettid(), getpid());

    if (!enable_unloader)
        return res;

    // Only perform unloading on the main thread
    if (gettid() != getpid())
        return res;

    if (should_unmap_zygisk) {
        unhook_functions();

        lsplt_free_resources();

        if (should_unmap_zygisk) {
            // Because both `pthread_attr_setstacksize` and `dlclose` have the same function signature,
            // we can use `musttail` to let the compiler reuse our stack frame and thus
            // `dlclose` will directly return to the caller of `pthread_attr_setstacksize`.
            LOGD("unmap libzygisk.so loaded at %p with size %zu", start_addr, block_size);

            [[clang::musttail]] return munmap(start_addr, block_size);
        }
    }

    return res;
}

void initialize_jni_hook();

DCL_HOOK_FUNC(char *, strdup, const char *s) {
  if (strcmp(s, "com.android.internal.os.ZygoteInit") == 0) {
      LOGV("strdup %s", s);
      initialize_jni_hook();
    }

    return old_strdup(s);
}

/*
 * INFO: Our goal is to get called after libart.so is loaded, but before ART actually starts running.
 * If we are too early, we won't find libart.so in maps, and if we are too late, we could make other
 * threads crash if they try to use the PLT while we are in the process of hooking it.
 * For this task, hooking property_get was chosen as there are lots of calls to this, so it's
 * relatively unlikely to break.
 *
 * The line where libart.so is loaded is:
 * https://github.com/aosp-mirror/platform_frameworks_base/blob/1cdfff555f4a21f71ccc978290e2e212e2f8b168/core/jni/AndroidRuntime.cpp#L1266
 *
 * And shortly after that, in the startVm method that is called right after, there are many calls to property_get:
 * https://github.com/aosp-mirror/platform_frameworks_base/blob/1cdfff555f4a21f71ccc978290e2e212e2f8b168/core/jni/AndroidRuntime.cpp#L791
 *
 * After we succeed in getting called at a point where libart.so is already loaded, we will ignore
 * the rest of the property_get calls.
 */
DCL_HOOK_FUNC(int, property_get, const char *key, char *value, const char *default_value) {
    hook_unloader();
    return old_property_get(key, value, default_value);
}

#undef DCL_HOOK_FUNC

// -----------------------------------------------------------------

static bool can_hook_jni = false;
static jint MODIFIER_NATIVE = 0;
static jmethodID member_getModifiers = nullptr;

void hookJniNativeMethods(JNIEnv *env, const char *clz, JNINativeMethod *methods, int numMethods) {
    if (!can_hook_jni) return;
    auto clazz = env->FindClass(clz);
    if (clazz == nullptr) {
        env->ExceptionClear();
        for (int i = 0; i < numMethods; i++) {
            methods[i].fnPtr = nullptr;
        }
        return;
    }

    vector<JNINativeMethod> hooks;
    for (int i = 0; i < numMethods; i++) {
        auto &nm = methods[i];
        auto mid = env->GetMethodID(clazz, nm.name, nm.signature);
        bool is_static = false;
        if (mid == nullptr) {
            env->ExceptionClear();
            mid = env->GetStaticMethodID(clazz, nm.name, nm.signature);
            is_static = true;
        }
        if (mid == nullptr) {
            env->ExceptionClear();
            nm.fnPtr = nullptr;
            continue;
        }
        auto method = lsplant::JNI_ToReflectedMethod(env, clazz, mid, is_static);
        auto modifier = lsplant::JNI_CallIntMethod(env, method, member_getModifiers);
        if ((modifier & MODIFIER_NATIVE) == 0) {
            nm.fnPtr = nullptr;
            continue;
        }
        auto artMethod = lsplant::art::ArtMethod::FromReflectedMethod(env, method);
        hooks.push_back(nm);
        auto orig = artMethod->GetData();
        LOGV("replaced %s %s orig %p", clz, nm.name, orig);
        nm.fnPtr = orig;
    }

    if (hooks.empty()) return;
    env->RegisterNatives(clazz, hooks.data(), hooks.size());
}

// JNI method hook definitions, auto generated
#include "jni_hooks.hpp"

void initialize_jni_hook() {
    auto get_created_java_vms = reinterpret_cast<jint (*)(JavaVM **, jsize, jsize *)>(
            dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (!get_created_java_vms) {
        struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
        if (!map_infos) {
            LOGE("Failed to scan maps for self");

            return;
        }

        for (size_t i = 0; i < map_infos->length; i++) {
            struct lsplt_map_entry map = map_infos->maps[i];

            if (!strstr(map.path, "/libnativehelper.so")) continue;
            void *h = dlopen(map.path, RTLD_LAZY);
            if (!h) {
                LOGW("cannot dlopen libnativehelper.so: %s", dlerror());
                break;
            }
            get_created_java_vms = reinterpret_cast<decltype(get_created_java_vms)>(dlsym(h, "JNI_GetCreatedJavaVMs"));
            dlclose(h);
            break;
        }

        lsplt_free_maps(map_infos);

        if (!get_created_java_vms) {
            LOGW("JNI_GetCreatedJavaVMs not found");
            return;
        }
    }
    JavaVM *vm = nullptr;
    jsize num = 0;
    jint res = get_created_java_vms(&vm, 1, &num);
    if (res != JNI_OK || vm == nullptr) return;
    JNIEnv *env = nullptr;
    res = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (res != JNI_OK || env == nullptr) return;

    auto classMember = lsplant::JNI_FindClass(env, "java/lang/reflect/Member");
    if (classMember != nullptr) member_getModifiers = lsplant::JNI_GetMethodID(env, classMember, "getModifiers", "()I");
    auto classModifier = lsplant::JNI_FindClass(env, "java/lang/reflect/Modifier");
    if (classModifier != nullptr) {
        auto fieldId = lsplant::JNI_GetStaticFieldID(env, classModifier, "NATIVE", "I");
        if (fieldId != nullptr) MODIFIER_NATIVE = lsplant::JNI_GetStaticIntField(env, classModifier, fieldId);
    }
    if (member_getModifiers == nullptr || MODIFIER_NATIVE == 0) return;
    if (!lsplant::art::ArtMethod::Init(env)) {
        LOGE("failed to init ArtMethod");
        return;
    }

    can_hook_jni = true;
    do_hook_zygote(env);
}

// -----------------------------------------------------------------

ZygiskModule::ZygiskModule(int id, void *handle, void *entry)
: id(id), handle(handle), entry{entry}, api{}, mod{nullptr} {
    // Make sure all pointers are null
    memset(&api, 0, sizeof(api));
    api.base.impl = this;
    api.base.registerModule = &ZygiskModule::RegisterModuleImpl;
}

bool ZygiskModule::RegisterModuleImpl(ApiTable *api, long *module) {
    if (api == nullptr || module == nullptr)
        return false;

    long api_version = *module;
    // Unsupported version
    if (api_version > ZYGISK_API_VERSION)
        return false;

    // Set the actual module_abi*
    api->base.impl->mod = { module };

    // Fill in API accordingly with module API version
    if (api_version >= 1) {
        api->v1.hookJniNativeMethods = hookJniNativeMethods;
        api->v1.pltHookRegister = [](auto a, auto b, auto c, auto d) {
            if (g_ctx) g_ctx->plt_hook_register(a, b, c, d);
        };
        api->v1.pltHookExclude = [](auto a, auto b) {
            if (g_ctx) g_ctx->plt_hook_exclude(a, b);
        };
        api->v1.pltHookCommit = []() { return g_ctx && g_ctx->plt_hook_commit(); };
        api->v1.connectCompanion = [](ZygiskModule *m) { return m->connectCompanion(); };
        api->v1.setOption = [](ZygiskModule *m, auto opt) { m->setOption(opt); };
    }
    if (api_version >= 2) {
        api->v2.getModuleDir = [](ZygiskModule *m) { return m->getModuleDir(); };
        api->v2.getFlags = [](auto) { return ZygiskModule::getFlags(); };
    }
    if (api_version >= 4) {
        api->v4.pltHookCommit = []() { return lsplt_commit_hook(); };
        api->v4.pltHookRegister = [](dev_t dev, ino_t inode, const char *symbol, void *fn, void **backup) {
            if (dev == 0 || inode == 0 || symbol == nullptr || fn == nullptr)
                return;
            lsplt_register_hook(dev, inode, symbol, fn, backup);
        };
        api->v4.exemptFd = [](int fd) { return g_ctx && g_ctx->exempt_fd(fd); };
    }

    return true;
}

void ZygiskContext::plt_hook_register(const char *regex, const char *symbol, void *fn, void **backup) {
    if (regex == nullptr || symbol == nullptr || fn == nullptr)
        return;
    regex_t re;
    if (regcomp(&re, regex, REG_NOSUB) != 0)
        return;
    pthread_mutex_lock(&hook_info_lock);
    register_info.emplace_back(RegisterInfo{re, symbol, fn, backup});
    pthread_mutex_unlock(&hook_info_lock);
}

void ZygiskContext::plt_hook_exclude(const char *regex, const char *symbol) {
    if (!regex) return;
    regex_t re;
    if (regcomp(&re, regex, REG_NOSUB) != 0)
        return;
    pthread_mutex_lock(&hook_info_lock);
    ignore_info.emplace_back(IgnoreInfo{re, symbol ?: ""});
    pthread_mutex_unlock(&hook_info_lock);
}

void ZygiskContext::plt_hook_process_regex() {
    if (register_info.empty())
        return;

    struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
    if (!map_infos) {
        LOGE("Failed to scan maps for self");

        return;
    }

    for (size_t i = 0; i < map_infos->length; i++) {
        struct lsplt_map_entry map = map_infos->maps[i];

        if (map.offset != 0 || !map.is_private || !(map.perms & PROT_READ)) continue;
        for (auto &reg: register_info) {
            if (regexec(&reg.regex, map.path, 0, nullptr, 0) != 0)
                continue;
            bool ignored = false;
            for (auto &ign: ignore_info) {
                if (regexec(&ign.regex, map.path, 0, nullptr, 0) != 0)
                    continue;
                if (ign.symbol.empty() || ign.symbol == reg.symbol) {
                    ignored = true;
                    break;
                }
            }
            if (!ignored) {
                lsplt_register_hook(map.dev, map.inode, reg.symbol.c_str(), reg.callback, reg.backup);
            }
        }
    }

    lsplt_free_maps(map_infos);
}

bool ZygiskContext::plt_hook_commit() {
    {
        pthread_mutex_lock(&hook_info_lock);
        plt_hook_process_regex();
        register_info.clear();
        ignore_info.clear();
        pthread_mutex_unlock(&hook_info_lock);
    }

    return lsplt_commit_hook();
}


bool ZygiskModule::valid() const {
    if (mod.api_version == nullptr)
        return false;
    switch (*mod.api_version) {
        case 4:
        case 3:
        case 2:
        case 1:
            return mod.v1->impl && mod.v1->preAppSpecialize && mod.v1->postAppSpecialize &&
                   mod.v1->preServerSpecialize && mod.v1->postServerSpecialize;
        default:
            return false;
    }
}

/* Zygisksu changed: Use own zygiskd */
int ZygiskModule::connectCompanion() const {
    return rezygiskd_connect_companion(id);
}

/* Zygisksu changed: Use own zygiskd */
int ZygiskModule::getModuleDir() const {
    return rezygiskd_get_module_dir(id);
}

void ZygiskModule::setOption(zygisk::Option opt) {
    if (g_ctx == nullptr)
        return;
    switch (opt) {
    case zygisk::FORCE_DENYLIST_UNMOUNT:
        g_ctx->flags[DO_REVERT_UNMOUNT] = true;
        break;
    case zygisk::DLCLOSE_MODULE_LIBRARY:
        unload = true;
        break;
    }
}

uint32_t ZygiskModule::getFlags() {
    return g_ctx ? (g_ctx->info_flags & ~PRIVATE_MASK) : 0;
}

// -----------------------------------------------------------------

int sigmask(int how, int signum) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signum);
    return sigprocmask(how, &set, nullptr);
}

void ZygiskContext::fork_pre() {
    /* INFO: Do our own fork before loading any 3rd party code.
             First block SIGCHLD, unblock after original fork is done.
    */
    sigmask(SIG_BLOCK, SIGCHLD);
    pid = old_fork();
    if (pid != 0 || flags[SKIP_FD_SANITIZATION])
        return;

    /* INFO: Record all open fds */
    DIR *dir = opendir("/proc/self/fd");
    if (dir == nullptr) {
        PLOGE("Failed to open /proc/self/fd");

        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        int fd = parse_int(entry->d_name);
        if (fd < 0 || fd >= MAX_FD_SIZE) {
            close(fd);

            continue;
        }

        allowed_fds[fd] = true;
    }

    /* INFO: The dirfd should not be allowed */
    allowed_fds[dirfd(dir)] = false;

    closedir(dir);
}

void ZygiskContext::sanitize_fds() {
    if (flags[SKIP_FD_SANITIZATION])
        return;

    if (flags[APP_FORK_AND_SPECIALIZE]) {
        auto update_fd_array = [&](int off) -> jintArray {
            if (exempted_fds.empty())
                return nullptr;

            jintArray array = env->NewIntArray(static_cast<int>(off + exempted_fds.size()));
            if (array == nullptr)
                return nullptr;

            env->SetIntArrayRegion(array, off, static_cast<int>(exempted_fds.size()), exempted_fds.data());
            for (int fd : exempted_fds) {
                if (fd >= 0 && fd < MAX_FD_SIZE) {
                    allowed_fds[fd] = true;
                }
            }
            *args.app->fds_to_ignore = array;
            flags[SKIP_FD_SANITIZATION] = true;
            return array;
        };

        if (jintArray fdsToIgnore = *args.app->fds_to_ignore) {
            int *arr = env->GetIntArrayElements(fdsToIgnore, nullptr);
            int len = env->GetArrayLength(fdsToIgnore);
            for (int i = 0; i < len; ++i) {
                int fd = arr[i];
                if (fd >= 0 && fd < MAX_FD_SIZE) {
                    allowed_fds[fd] = true;
                }
            }
            if (jintArray newFdList = update_fd_array(len)) {
                env->SetIntArrayRegion(newFdList, 0, len, arr);
            }
            env->ReleaseIntArrayElements(fdsToIgnore, arr, JNI_ABORT);
        } else {
            update_fd_array(0);
        }
    }

    if (pid != 0)
        return;

    // Close all forbidden fds to prevent crashing
    DIR *dir = opendir("/proc/self/fd");
    if (dir == nullptr) {
        PLOGE("Failed to open /proc/self/fd");

        return;
    }

    int dfd = dirfd(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        int fd = parse_int(entry->d_name);
        if (fd < 0 || fd > MAX_FD_SIZE || fd == dfd || allowed_fds[fd]) continue;

        close(fd);

        LOGW("Closed leaked fd: %d", fd);
    }

    closedir(dir);
}

void ZygiskContext::fork_post() {
    // Unblock SIGCHLD in case the original method didn't
    sigmask(SIG_UNBLOCK, SIGCHLD);
    g_ctx = nullptr;
}

bool ZygiskContext::load_modules_only() {
  struct zygisk_modules ms;
  if (rezygiskd_read_modules(&ms) == false) {
    LOGE("Failed to read modules from zygiskd");

    return false;
  }

  for (size_t i = 0; i < ms.modules_count; i++) {
    char *lib_path = ms.modules[i];

    void *handle = dlopen(lib_path, RTLD_NOW);
    if (!handle) {
      LOGE("Failed to load module [%s]: %s", lib_path, dlerror());

      continue;
    }

    void *entry = dlsym(handle, "zygisk_module_entry");
    if (!entry) {
      LOGE("Failed to find entry point in module [%s]: %s", lib_path, dlerror());

      dlclose(handle);

      continue;
    }

    modules.emplace_back(i, handle, entry);
  }

  free_modules(&ms);

  return true;
}

/* Zygisksu changed: Load module fds */
void ZygiskContext::run_modules_pre() {
  for (auto &m : modules) {
    m.onLoad(env);

    if (flags[APP_SPECIALIZE]) m.preAppSpecialize(args.app);
    else if (flags[SERVER_FORK_AND_SPECIALIZE]) m.preServerSpecialize(args.server);
  }
}

void ZygiskContext::run_modules_post() {
    flags[POST_SPECIALIZE] = true;

    size_t modules_unloaded = 0;
    for (const auto &m : modules) {
        if (flags[APP_SPECIALIZE]) m.postAppSpecialize(args.app);
        else if (flags[SERVER_FORK_AND_SPECIALIZE]) m.postServerSpecialize(args.server);

        if (m.tryUnload()) modules_unloaded++;
    }

    if (modules.size() > 0) {
        LOGD("modules unloaded: %zu/%zu", modules_unloaded, modules.size());

        /* INFO: While Variable Length Arrays (VLAs) aren't usually
                   recommended due to the ease of using too much of the
                   stack, this should be fine since it should not be
                   possible to exhaust the stack with only a few addresses. */
        void *module_addrs[modules.size() * sizeof(void *)];

        size_t i = 0;
        for (const auto &m : modules) {
            module_addrs[i++] = m.getEntry();
        }

        clean_trace("/data/adb", module_addrs, modules.size(), modules.size(), modules_unloaded);
    }
}

/* Zygisksu changed: Load module fds */
void ZygiskContext::app_specialize_pre() {
    flags[APP_SPECIALIZE] = true;

    /* INFO: Isolated services have different UIDs than the main apps. Because
               numerous root implementations base themselves in the UID of the
               app, we need to ensure that the UID sent to ReZygiskd to search
               is the app's and not the isolated service, or else it will be
               able to bypass DenyList.

             All apps, and isolated processes, of *third-party* applications will
               have their app_data_dir set. The system applications might not have
               one, however it is unlikely they will create an isolated process,
               and even if so, it should not impact in detections, performance or
               any area.
    */
    uid_t uid = args.app->uid;
    if (IS_ISOLATED_SERVICE(uid) && args.app->app_data_dir) {
        /* INFO: If the app is an isolated service, we use the UID of the
                   app's process data directory, which is the UID of the
                   app itself, which root implementations actually use.
        */
        const char *data_dir = env->GetStringUTFChars(args.app->app_data_dir, NULL);
        if (!data_dir) {
            LOGE("Failed to get app data directory");

            return;
        }

        struct stat st;
        if (stat(data_dir, &st) == -1) {
            PLOGE("Failed to stat app data directory [%s]", data_dir);

            env->ReleaseStringUTFChars(args.app->app_data_dir, data_dir);

            return;
        }

        uid = st.st_uid;

        LOGD("Isolated service being related to UID %d, app data dir: %s", uid, data_dir);

        env->ReleaseStringUTFChars(args.app->app_data_dir, data_dir);
    }

    info_flags = rezygiskd_get_process_flags(uid, (const char *const)process);
     if (info_flags & PROCESS_IS_FIRST_STARTED) {
        /* INFO: To ensure we are really using a clean mount namespace, we use
                   the first process it as reference for clean mount namespace,
                   before it even does something, so that it will be clean yet
                   with expected mounts.
        */
        update_mnt_ns(Clean, true);
    }

    if ((info_flags & PROCESS_IS_MANAGER) == PROCESS_IS_MANAGER) {
        LOGD("Manager process detected. Notifying that Zygisk has been enabled.");

        /* INFO: This environment variable is related to Magisk Zygisk/Manager. It
                   it used by Magisk's Zygisk to communicate to Magisk Manager whether
                   Zygisk is working or not, allowing Zygisk modules to both work properly
                   and for the manager to mark Zygisk as enabled.

                 However, to enhance capabilities of root managers, it is also set for
                   any other supported manager, so that, if they wish, they can recognize
                   if Zygisk is enabled.
        */
        setenv("ZYGISK_ENABLED", "1", 1);
    } else {
        /* INFO: Because we load directly from the file, we need to do it before we umount
                   the mounts, or else it won't have access to /data/adb anymore.
        */
        if (!load_modules_only()) {
            LOGE("Failed to load modules");

            return;
        }

        /* INFO: Modules only have two "start off" points from Zygisk, preSpecialize and
                   postSpecialize. In preSpecialize, the process still has privileged 
                   permissions, and therefore can execute mount/umount/setns functions.
                   If we update the mount namespace AFTER executing them, any mounts made
                   will be lost, and the process will not have access to them anymore.

                 In postSpecialize, while still could have its mounts modified with the
                   assistance of a Zygisk companion, it will already have the mount
                   namespace switched by then, so there won't be issues.

                 Knowing this, we update the mns before execution, so that they can still
                   make changes to mounts in DenyListed processes without being reverted.
        */
        bool in_denylist = (info_flags & PROCESS_ON_DENYLIST) == PROCESS_ON_DENYLIST;
        if (in_denylist) {
            flags[DO_REVERT_UNMOUNT] = true;

            update_mnt_ns(Clean, false);
        }

        /* INFO: Executed after setns to ensure a module can update the mounts of an 
                   application without worrying about it being overwritten by setns.
        */
        run_modules_pre();

        /* INFO: The modules may request that although the process is NOT in
                   the DenyList, it has its mount namespace switched to the clean
                   one.

                 So to ensure this behavior happens, we must also check after the
                   modules are loaded and executed, so that the modules can have
                   the chance to request it.
        */
        if (!in_denylist && flags[DO_REVERT_UNMOUNT])
            update_mnt_ns(Clean, false);
    }
}


void ZygiskContext::app_specialize_post() {
    run_modules_post();

    // Cleanups
    env->ReleaseStringUTFChars(args.app->nice_name, process);
    g_ctx = nullptr;
}

bool ZygiskContext::exempt_fd(int fd) {
    if (flags[POST_SPECIALIZE] || flags[SKIP_FD_SANITIZATION])
        return true;
    if (!flags[APP_FORK_AND_SPECIALIZE])
        return false;
    exempted_fds.push_back(fd);
    return true;
}

// -----------------------------------------------------------------

void ZygiskContext::nativeSpecializeAppProcess_pre() {
    process = env->GetStringUTFChars(args.app->nice_name, nullptr);
    LOGV("pre specialize [%s]", process);
    // App specialize does not check FD
    flags[SKIP_FD_SANITIZATION] = true;
    app_specialize_pre();
}

void ZygiskContext::nativeSpecializeAppProcess_post() {
    LOGV("post specialize [%s]", process);
    app_specialize_post();
}

/* Zygisksu changed: No system_server status write back */
void ZygiskContext::nativeForkSystemServer_pre() {
    LOGV("pre forkSystemServer");
    flags[SERVER_FORK_AND_SPECIALIZE] = true;

    fork_pre();
    if (!is_child())
      return;

    load_modules_only();
    run_modules_pre();
    rezygiskd_system_server_started();

    sanitize_fds();
}

void ZygiskContext::nativeForkSystemServer_post() {
    if (pid == 0) {
        LOGV("post forkSystemServer");
        run_modules_post();
    }
    fork_post();
}

void ZygiskContext::nativeForkAndSpecialize_pre() {
    process = env->GetStringUTFChars(args.app->nice_name, nullptr);
    LOGV("pre forkAndSpecialize [%s]", process);
    flags[APP_FORK_AND_SPECIALIZE] = true;

    fork_pre();
    if (!is_child())
        return;

    app_specialize_pre();

    sanitize_fds();
}

void ZygiskContext::nativeForkAndSpecialize_post() {
    if (pid == 0) {
        LOGV("post forkAndSpecialize [%s]", process);
        app_specialize_post();
    }
    fork_post();
}

ZygiskContext::~ZygiskContext() {
    // This global pointer points to a variable on the stack.
    // Set this to nullptr to prevent leaking local variable.
    // This also disables most plt hooked functions.
    g_ctx = nullptr;

    if (!is_child())
        return;

    should_unmap_zygisk = true;

    // Unhook JNI methods
    for (const auto &[clz, methods] : *jni_hook_list) {
        if (!methods.empty() && env->RegisterNatives(
                env->FindClass(clz.data()), methods.data(),
                static_cast<int>(methods.size())) != 0) {
            LOGE("Failed to restore JNI hook of class [%s]", clz.data());
            should_unmap_zygisk = false;
        }
    }
    delete jni_hook_list;
    jni_hook_list = nullptr;

    // Strip out all API function pointers
    for (auto &m : modules) {
        m.clearApi();
    }

    enable_unloader = true;
}

} // namespace

static bool hook_commit(struct lsplt_map_info *map_infos) {
    if (map_infos ? lsplt_commit_hook_manual(map_infos) : lsplt_commit_hook()) {
        return true;
    } else {
        LOGE("plt_hook failed");
        return false;
    }
}

static void hook_register(dev_t dev, ino_t inode, const char *symbol, void *new_func, void **old_func) {
    if (!lsplt_register_hook(dev, inode, symbol, new_func, old_func)) {
        LOGE("Failed to register plt_hook \"%s\"", symbol);
        return;
    }
    plt_hook_list->emplace_back(dev, inode, symbol, old_func);
}

#define PLT_HOOK_REGISTER_SYM(DEV, INODE, SYM, NAME) \
    hook_register(DEV, INODE, SYM, (void*) new_##NAME, (void **) &old_##NAME)

#define PLT_HOOK_REGISTER(DEV, INODE, NAME) \
    PLT_HOOK_REGISTER_SYM(DEV, INODE, #NAME, NAME)

/* INFO: module_addrs_length is always the same as "load" */
void clean_trace(const char *path, void **module_addrs, size_t module_addrs_length, size_t load, size_t unload) {
    LOGD("cleaning trace for path %s", path);

    if (load > 0 || unload > 0) solist_reset_counters(load, unload);

    LOGD("Dropping solist record for %s", path);

    for (size_t i = 0; i < module_addrs_length; i++) {
        bool has_dropped = solist_drop_so_path(module_addrs[i]);
        if (!has_dropped) continue;

        LOGD("Dropped solist record for %p", module_addrs[i]);
    }
}

void hook_functions() {
    plt_hook_list = new vector<tuple<dev_t, ino_t, const char *, void **>>();
    jni_hook_list = new map<string, vector<JNINativeMethod>>();

    ino_t android_runtime_inode = 0;
    dev_t android_runtime_dev = 0;

    struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
    if (!map_infos) {
        LOGE("Failed to scan maps for self");

        return;
    }

    for (size_t i = 0; i < map_infos->length; i++) {
        struct lsplt_map_entry map = map_infos->maps[i];

        if (!strstr(map.path, "libandroid_runtime.so")) continue;

        android_runtime_inode = map.inode;
        android_runtime_dev = map.dev;

        LOGD("Found libandroid_runtime.so at [%zu:%lu]", android_runtime_dev, android_runtime_inode);

        break;
    }

    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, fork);
    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, strdup);
    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, property_get);
    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, _ZNK18FileDescriptorInfo14ReopenOrDetachERKNSt3__18functionIFvNS0_12basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEEEE);
    hook_commit(map_infos);

    lsplt_free_maps(map_infos);

    // Remove unhooked methods
    plt_hook_list->erase(
            std::remove_if(plt_hook_list->begin(), plt_hook_list->end(),
                           [](auto &t) { return *std::get<3>(t) == nullptr;}),
            plt_hook_list->end());
}

static void hook_unloader() {
    if (hooked_unloader) return;
    hooked_unloader = true;

    ino_t art_inode = 0;
    dev_t art_dev = 0;

    struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
    if (!map_infos) {
        LOGE("Failed to scan maps for self");

        return;
    }

    for (size_t i = 0; i < map_infos->length; i++) {
        struct lsplt_map_entry map = map_infos->maps[i];

        if (strstr(map.path, "/libart.so") != nullptr) {
            art_inode = map.inode;
            art_dev = map.dev;

            LOGD("Found libart.so at [%zu:%lu]", art_dev, art_inode);

            break;
        }
    }

    if (art_dev == 0 || art_inode == 0) {
        /*
         * INFO: If we are here, it means we are too early and libart.so hasn't loaded yet when
         * property_get was called. This doesn't normally happen, but we try again next time
         * just to be safe.
         */

        LOGE("virtual map for libart.so is not cached");

        hooked_unloader = false;
    } else {
        LOGD("hook_unloader called with libart.so [%zu:%lu]", art_dev, art_inode);

        PLT_HOOK_REGISTER(art_dev, art_inode, pthread_attr_setstacksize);
        hook_commit(map_infos);
    }

    lsplt_free_maps(map_infos);
}

static void unhook_functions() {
    // Unhook plt_hook
    for (const auto &[dev, inode, sym, old_func] : *plt_hook_list) {
        if (!lsplt_register_hook(dev, inode, sym, *old_func, NULL)) {
            LOGE("Failed to register plt_hook [%s]", sym);
        }
    }
    delete plt_hook_list;
    if (!hook_commit(NULL)) {
        LOGE("Failed to restore plt_hook");
        should_unmap_zygisk = false;
    }
}
