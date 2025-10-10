/**
 * BMP frame saving module for C64 Ultimate streaming
 * Handles individual frame capture as BMP files for debugging
 */

#ifndef C64_RECORD_FRAMES_H
#define C64_RECORD_FRAMES_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// BMP frame saving functions
void c64_frames_save_as_bmp(struct c64_source *context, uint32_t *frame_buffer);

#endif  // C64_RECORD_FRAMES_H
