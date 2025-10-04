#ifndef C64U_LOGGING_H
#define C64U_LOGGING_H

#include <obs-module.h>
#include <time.h>
#include <stdint.h>

// Logging control - using extern to avoid multiple definitions
extern bool c64u_debug_logging;

// Fast milliseconds since epoch for logging
static inline uint64_t c64u_get_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

#define C64U_LOG_INFO(format, ...)                                                                        \
    do {                                                                                                   \
        blog(LOG_INFO, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);          \
    } while (0)

#define C64U_LOG_DEBUG(format, ...)                                                                       \
    do {                                                                                                   \
        if (c64u_debug_logging) {                                                                          \
            blog(LOG_DEBUG, "[C64U %llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__); \
        }                                                                                                  \
    } while (0)

#define C64U_LOG_WARNING(format, ...)                                                                     \
    do {                                                                                                   \
        blog(LOG_WARNING, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);       \
    } while (0)

#define C64U_LOG_ERROR(format, ...)                                                                       \
    do {                                                                                                   \
        blog(LOG_ERROR, "[%llu] " format, (unsigned long long)c64u_get_millis(), ##__VA_ARGS__);         \
    } while (0)

#endif // C64U_LOGGING_H
