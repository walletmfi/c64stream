#ifndef C64_NETWORK_BUFFER_H
#define C64_NETWORK_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C64_VIDEO_PACKET_SIZE 780
#define C64_AUDIO_PACKET_SIZE 770

#define C64_MAX_VIDEO_RATE 3400
#define C64_MAX_AUDIO_RATE 250
#define C64_MAX_DELAY_MS 2000

#define C64_MAX_VIDEO_PACKETS ((C64_MAX_VIDEO_RATE * C64_MAX_DELAY_MS) / 1000)
#define C64_MAX_AUDIO_PACKETS ((C64_MAX_AUDIO_RATE * C64_MAX_DELAY_MS) / 1000)

struct c64_network_buffer;

// Create/destroy buffer (allocates maximum size buffers)
struct c64_network_buffer *c64_network_buffer_create(void);
void c64_network_buffer_destroy(struct c64_network_buffer *buf);

// Set dynamic delay in milliseconds (flushes buffer)
void c64_network_buffer_set_delay(struct c64_network_buffer *buf, size_t video_delay_ms, size_t audio_delay_ms);

// Push packet from network thread
void c64_network_buffer_push_video(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_us);
void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_us);

// Pop the latest aligned pair for render thread
// Returns 1 if a pair is available, 0 if buffers empty
int c64_network_buffer_pop(struct c64_network_buffer *buf, const uint8_t **video_data, size_t *video_size,
                           const uint8_t **audio_data, size_t *audio_size, uint64_t *timestamp_us);

// Flush all buffers (clear all pending data)
void c64_network_buffer_flush(struct c64_network_buffer *buf);

#ifdef __cplusplus
}
#endif

#endif // C64_NETWORK_BUFFER_H