#ifndef C64U_VIDEO_H
#define C64U_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64u_source;
struct frame_assembly;

// VIC color palette (BGRA values for OBS) - converted from grab.py RGB values
extern const uint32_t vic_colors[16];

// Helper functions for frame assembly
void init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num);
bool is_frame_complete(struct frame_assembly *frame);
bool is_frame_timeout(struct frame_assembly *frame);
void swap_frame_buffers(struct c64u_source *context);
void assemble_frame_to_buffer(struct c64u_source *context, struct frame_assembly *frame);

// Delay queue management
void init_delay_queue(struct c64u_source *context);
bool enqueue_delayed_frame(struct c64u_source *context, struct frame_assembly *frame, uint16_t sequence_num);
bool dequeue_delayed_frame(struct c64u_source *context);
void clear_delay_queue(struct c64u_source *context);

// Video thread function
void *video_thread_func(void *data);

// Frame saving and recording functions
void save_frame_as_bmp(struct c64u_source *context, uint32_t *frame_buffer);
void start_video_recording(struct c64u_source *context);
void record_video_frame(struct c64u_source *context, uint32_t *frame_buffer);
void record_audio_data(struct c64u_source *context, const uint8_t *audio_data, size_t data_size);
void stop_video_recording(struct c64u_source *context);

#endif // C64U_VIDEO_H
