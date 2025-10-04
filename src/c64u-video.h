#ifndef C64U_VIDEO_H
#define C64U_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "c64u-color.h"

// Rendering defaults
#define C64U_DEFAULT_RENDER_DELAY_FRAMES 3  // Default frame delay to smooth UDP packet loss/reordering
#define C64U_MAX_RENDER_DELAY_FRAMES 100    // Maximum allowed render delay frames
#define C64U_RENDER_BUFFER_SAFETY_MARGIN 10 // Extra buffer frames for queue safety

// Timing constants (nanoseconds)
#define C64U_FRAME_TIMEOUT_NS 100000000ULL       // 100ms - timeout for frame freshness (5 frames @ 50Hz)
#define C64U_DEBUG_LOG_INTERVAL_NS 2000000000ULL // 2 seconds - interval for debug logging

// Forward declarations
struct c64u_source;
struct frame_assembly;

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

// Performance optimization functions
void process_video_statistics_batch(struct c64u_source *context, uint64_t current_time);
void process_audio_statistics_batch(struct c64u_source *context, uint64_t current_time);

// Lock-free frame assembly functions
void init_frame_assembly_lockfree(struct frame_assembly *frame, uint16_t frame_num);
bool try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index);
bool is_frame_complete_lockfree(struct frame_assembly *frame);

// Video thread function
void *video_thread_func(void *data);

#endif // C64U_VIDEO_H
