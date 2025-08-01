#ifndef LOGGING_H
#define LOGGING_H

#include <android/log.h>
#include <errno.h>
#include <string.h>

#ifndef LOG_TAG
  #ifdef __LP64__
    #define LOG_TAG "zygisk-core64"
  #else
    #define LOG_TAG "zygisk-core32"
  #endif
#endif

#ifndef NDEBUG
  #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
  #define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
  #define LOGD(...)
  #define LOGV(...)
#endif

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)
#define PLOGE(fmt, args...) LOGE(fmt " failed with %d: %s", ##args, errno, strerror(errno))

#endif /* LOGGING_H */
