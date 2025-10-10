/**
 * Network packet recording for C64 Ultimate streaming
 * Provides detailed network-level analysis for debugging streaming performance
 */

#ifndef C64_RECORD_NETWORK_H
#define C64_RECORD_NETWORK_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// Network packet recording functions
void c64_network_write_header(struct c64_source *context);
void c64_network_log_video_packet(struct c64_source *context, uint16_t sequence_num, uint16_t frame_num,
                                  uint16_t line_num, bool is_last_packet, size_t packet_size, size_t data_payload,
                                  int64_t jitter_us);
void c64_network_log_audio_packet(struct c64_source *context, uint16_t sequence_num, size_t packet_size,
                                  uint16_t sample_count, int64_t jitter_us);

#endif  // C64_RECORD_NETWORK_H
