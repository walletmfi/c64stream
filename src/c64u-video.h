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

// Video thread function
void *video_thread_func(void *data);

#endif // C64U_VIDEO_H