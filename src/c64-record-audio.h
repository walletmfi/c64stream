/**
 * WAV audio recording module for C64 Ultimate streaming
 * Handles 16-bit stereo PCM audio recording with proper WAV structure
 */

#ifndef C64_RECORD_AUDIO_H
#define C64_RECORD_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Forward declarations
struct c64_source;

// Audio recording functions (WAV format)
void c64_audio_write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
void c64_audio_finalize_wav_header(FILE *file, uint32_t data_size);
void c64_audio_record_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size);

#endif  // C64_RECORD_AUDIO_H
