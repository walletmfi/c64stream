/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-file.h"
#include "c64-logging.h"

#include <sys/stat.h>
#include <util/platform.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif
#endif

/**
 * Create directory recursively (equivalent to mkdir -p)
 * @param path Directory path to create
 * @return true if successful or directory exists, false on error
 */
bool c64_create_directory_recursive(const char *path)
{
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\')
        tmp[len - 1] = 0;

    // Start from the beginning, but skip drive letters on Windows (e.g., "C:")
    p = tmp;
    if (len > 1 && tmp[1] == ':') {
        p = tmp + 2; // Skip "C:" part on Windows
    }
    if (*p == '/' || *p == '\\') {
        p++; // Skip leading slash
    }

    // Create each directory in the path
    for (; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            if (os_mkdir(tmp) != 0) {
                // Check if it already exists (ignore error if it does)
                struct stat st;
                if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    // Directory creation failed and it doesn't exist
                    return false;
                }
            }
            *p = '/'; // Use forward slash consistently (works on Windows too)
        }
    }

    // Create the final directory
    if (os_mkdir(tmp) != 0) {
        struct stat st;
        if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    return true;
}

/**
 * Get the current user's Documents folder path
 * @param path_buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if successful, false on error
 */
bool c64_get_user_documents_path(char *path_buffer, size_t buffer_size)
{
    if (!path_buffer || buffer_size < 32) {
        return false;
    }

#ifdef _WIN32
    // Windows: Use SHGetFolderPath to get the current user's Documents folder
    WCHAR documents_path_w[MAX_PATH];
    char documents_path[MAX_PATH];

    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, documents_path_w);
    if (SUCCEEDED(hr)) {
        // Convert wide string to multi-byte string
        int result =
            WideCharToMultiByte(CP_UTF8, 0, documents_path_w, -1, documents_path, sizeof(documents_path), NULL, NULL);
        if (result > 0) {
            strncpy(path_buffer, documents_path, buffer_size - 1);
            path_buffer[buffer_size - 1] = '\0';
            blog(LOG_DEBUG, "[C64] Retrieved Windows Documents path: %s", path_buffer);
            return true;
        } else {
            blog(LOG_WARNING, "[C64] Failed to convert Windows Documents path to UTF-8");
        }
    } else {
        blog(LOG_WARNING, "[C64] Failed to get Windows Documents folder path (HRESULT: 0x%08X)", hr);
    }

    // Fallback to Public Documents if personal Documents fails
    strcpy(path_buffer, "C:\\Users\\Public\\Documents");
    blog(LOG_INFO, "[C64] Using fallback Windows Documents path: %s", path_buffer);
    return true;

#elif defined(__APPLE__)
    // macOS: Use NSDocumentDirectory
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_buffer, buffer_size, "%s/Documents", home);
        blog(LOG_DEBUG, "[C64] Retrieved macOS Documents path: %s", path_buffer);
        return true;
    } else {
        blog(LOG_WARNING, "[C64] Failed to get macOS home directory");
        strcpy(path_buffer, "/Users/Shared/Documents");
        blog(LOG_INFO, "[C64] Using fallback macOS Documents path: %s", path_buffer);
        return true;
    }

#else
    // Linux/Unix: Use XDG_DOCUMENTS_DIR or fallback to ~/Documents
    const char *xdg_documents = getenv("XDG_DOCUMENTS_DIR");
    if (xdg_documents) {
        strncpy(path_buffer, xdg_documents, buffer_size - 1);
        path_buffer[buffer_size - 1] = '\0';
        blog(LOG_DEBUG, "[C64] Retrieved Linux XDG Documents path: %s", path_buffer);
        return true;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_buffer, buffer_size, "%s/Documents", home);
        blog(LOG_DEBUG, "[C64] Retrieved Linux Documents path: %s", path_buffer);
        return true;
    } else {
        blog(LOG_WARNING, "[C64] Failed to get Linux home directory");
        strcpy(path_buffer, "/tmp");
        blog(LOG_INFO, "[C64] Using fallback Linux Documents path: %s", path_buffer);
        return true;
    }
#endif
}
