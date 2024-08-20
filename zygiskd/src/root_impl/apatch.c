#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "../constants.h"
#include "../utils.h"

#include "apatch.h"

enum RootImplState apatch_get_existence(void) {
  struct stat s;
  if (stat("/data/adb/apd", &s) != 0) {
    if (errno != ENOENT) LOGE("Failed to stat APatch apd binary: %s\n", strerror(errno));
    errno = 0;

    return Inexistent;
  }

  char apatch_version[32];
  char *const argv[] = { "apd", "-V", NULL };

  if (!exec_command(apatch_version, sizeof(apatch_version), "/data/adb/apd", argv)) {
    LOGE("Failed to execute apd binary: %s\n", strerror(errno));
    errno = 0;

    return Inexistent;
  }

  int version = atoi(apatch_version + strlen("apd "));

  if (version == 0) return Abnormal;
  if (version >= MIN_APATCH_VERSION && version <= 999999) return Supported;
  if (version >= 1 && version <= MIN_APATCH_VERSION - 1) return TooOld;

  return Inexistent;
}

struct package_config {
  uid_t uid;
  bool root_granted;
  bool umount_needed;
};

struct packages_config {
  struct package_config *configs;
  size_t size;
};

/* WARNING: Dynamic memory based */
bool _apatch_get_package_config(struct packages_config *restrict config) {
  FILE *fp = fopen("/data/adb/ap/package_config", "r");
  if (fp == NULL) {
    LOGE("Failed to open APatch's package_config: %s\n", strerror(errno));

    return false;
  }

  char line[1024];
  /* INFO: Skip the CSV header */
  fgets(line, sizeof(line), fp);

  LOGI("meow meow: %s\n", line);

  config->size = 0;
  while (fgets(line, sizeof(line), fp) != NULL) {
    LOGI("meow meow\n");
 
    config->configs = realloc(config->configs, (config->size + 1) * sizeof(struct package_config));
    if (config->configs == NULL) {
      LOGE("Failed to realloc APatch config struct: %s\n", strerror(errno));

      fclose(fp);

      return false;
    }

    LOGI("meow meow (1): %s\n", line);

    strtok(line, ",");

    LOGI("meow meow (2)\n");

    char *exclude_str = strtok(NULL, ",");
    if (exclude_str == NULL) continue;

    LOGI("meow meow: %s\n", exclude_str);

    char *allow_str = strtok(NULL, ",");
    if (allow_str == NULL) continue;

    LOGI("meow meow: %s\n", allow_str);

    char *uid_str = strtok(NULL, ",");
    if (uid_str == NULL) continue;

    LOGI("meow meow: %s\n", uid_str);

    config->configs[config->size].uid = atoi(uid_str);
    config->configs[config->size].root_granted = strcmp(allow_str, "1") == 0;
    config->configs[config->size].umount_needed = strcmp(exclude_str, "1") == 0;

    config->size++;
  }

  fclose(fp);

  return true;
}

bool apatch_uid_granted_root(uid_t uid) {
  struct packages_config *config = NULL;
  if (!_apatch_get_package_config(config)) {
    free(config);

    return false;
  }

  for (size_t i = 0; i < config->size; i++) {
    if (config->configs[i].uid == uid) {
      free(config);

      return config->configs[i].root_granted;
    }
  }

  free(config);

  return false;
}

bool apatch_uid_should_umount(uid_t uid) {
  struct packages_config *config = NULL;
  if (!_apatch_get_package_config(config)) {
    free(config);

    return false;
  }

  for (size_t i = 0; i < config->size; i++) {
    if (config->configs[i].uid == uid) {
      free(config);

      return config->configs[i].umount_needed;
    }
  }

  free(config);

  return false;
}

bool apatch_uid_is_manager(uid_t uid) {
  struct stat s;
  if (stat("/data/user_de/0/me.bmax.apatch", &s) == -1) {
    if (errno != ENOENT) LOGE("Failed to stat APatch manager data directory: %s\n", strerror(errno));
    errno = 0;

    return false;
  }

  return s.st_uid == uid;
}
