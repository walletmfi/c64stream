#include "c64-version.h"
#include "c64-logging.h" // For Windows snprintf compatibility
#include <stdio.h>
#include <time.h>

static char version_string[256] = {0};
static char build_info_string[512] = {0};

const char *c64_get_version_string(void)
{
    if (version_string[0] == '\0') {
        snprintf(version_string, sizeof(version_string), "C64 Stream Plugin %s (%s)", C64_VERSION_TAG, C64_GIT_HASH);
    }
    return version_string;
}

const char *c64_get_build_info(void)
{
    if (build_info_string[0] == '\0') {
        snprintf(build_info_string, sizeof(build_info_string), "Version: %s | Git: %s | Built: %s UTC", C64_VERSION_TAG,
                 C64_GIT_HASH, C64_BUILD_TIME);
    }
    return build_info_string;
}
