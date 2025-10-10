#ifndef C64_RECORD_H
#define C64_RECORD_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// Frame saving and recording functions
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
