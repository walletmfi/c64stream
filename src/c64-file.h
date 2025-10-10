/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Create directory recursively (equivalent to mkdir -p)
 * @param path Directory path to create
 * @return true if successful or directory exists, false on error
 */
bool c64_create_directory_recursive(const char *path);

/**
 * Get the current user's Documents folder path
 * @param path_buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if successful, false on error
 */
bool c64_get_user_documents_path(char *path_buffer, size_t buffer_size);
