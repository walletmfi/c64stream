/**
 * OBS timing recording for C64 Ultimate streaming
 * Provides detailed timing analysis for debugging audio/video synchronization
 */

#ifndef C64_RECORD_OBS_H
#define C64_RECORD_OBS_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// OBS timing management functions
void c64_obs_write_header(struct c64_source *context);
void c64_obs_log_video_event(struct c64_source *context, uint64_t calculated_timestamp_ms, uint64_t actual_timestamp_ms,
                             size_t frame_size);
void c64_obs_log_audio_event(struct c64_source *context, uint64_t calculated_timestamp_ms, uint64_t actual_timestamp_ms,
                             size_t data_size);

#endif  // C64_RECORD_OBS_H
