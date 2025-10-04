#ifndef C64_VERSION_H
#define C64_VERSION_H

/**
 * @file c64-version.h
 * @brief Version and build information for C64 Stream Plugin
 */

// Version information (will be populated by build system)
#ifndef C64_VERSION_TAG
#define C64_VERSION_TAG "dev"
#endif

#ifndef C64_GIT_HASH
#define C64_GIT_HASH "unknown"
#endif

#ifndef C64_BUILD_TIME
#define C64_BUILD_TIME "unknown"
#endif

// Function to get formatted version string
const char *c64_get_version_string(void);

// Function to get build information
const char *c64_get_build_info(void);

#endif /* C64_VERSION_H */
