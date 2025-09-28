#ifndef C64U_LOGGING_H
#define C64U_LOGGING_H

#include <obs-module.h>

// Logging control - using extern to avoid multiple definitions
extern bool c64u_debug_logging;

#define C64U_LOG_INFO(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			blog(LOG_INFO, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define C64U_LOG_WARNING(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			blog(LOG_WARNING, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define C64U_LOG_ERROR(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			blog(LOG_ERROR, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define C64U_LOG_DEBUG(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			blog(LOG_DEBUG, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#endif /* C64U_LOGGING_H */