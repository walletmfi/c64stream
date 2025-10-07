#ifndef C64_AUDIO_H
#define C64_AUDIO_H

#include <stdint.h>
#include <stddef.h>

// Forward declaration
struct c64_source;

// Audio thread function
void *audio_thread_func(void *data);

// Audio processing function
void c64_process_audio_packet(struct c64_source *context, const uint8_t *audio_data, size_t data_size,
                              uint64_t timestamp_ns);

#endif // C64_AUDIO_H
