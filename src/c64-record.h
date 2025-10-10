/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_H
#define C64_RECORD_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// Include module headers
#include "c64-record-obs.h"
#include "c64-record-network.h"
#include "c64-record-video.h"
#include "c64-record-audio.h"
#include "c64-record-frames.h"

// Session management functions
void c64_session_ensure_exists(struct c64_source *context);
bool c64_session_any_recording_active(struct c64_source *context);
void c64_session_cleanup_if_needed(struct c64_source *context);

// CSV recording functions
void c64_start_csv_recording(struct c64_source *context);
void c64_stop_csv_recording(struct c64_source *context);

// Network recording functions
void c64_start_network_recording(struct c64_source *context);
void c64_stop_network_recording(struct c64_source *context);

// Main entry point functions - delegate to appropriate modules
void c64_save_frame_as_bmp(struct c64_source *context, uint32_t *frame_buffer);
void c64_start_video_recording(struct c64_source *context);
void c64_record_video_frame(struct c64_source *context, uint32_t *frame_buffer);
void c64_record_audio_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size);
void c64_stop_video_recording(struct c64_source *context);

// Recording initialization and cleanup functions
void c64_record_init(struct c64_source *context);
void c64_record_cleanup(struct c64_source *context);
void c64_record_update_settings(struct c64_source *context, void *settings);

#endif  // C64_RECORD_H
