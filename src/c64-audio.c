#include <obs-module.h>
#include <util/platform.h>
#include "c64-network.h" // Include network header first to avoid Windows header conflicts

#include "c64-logging.h"
#include "c64-audio.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-video.h"  // For batch statistics processing
#include "c64-record.h" // For recording functions

// Audio thread function
void *audio_thread_func(void *data)
{
    struct c64_source *context = data;
    uint8_t packet[C64_AUDIO_PACKET_SIZE];

    C64_LOG_DEBUG("Audio receiver thread started on port %u", context->audio_port);

    while (context->thread_active) {
        ssize_t received = recv(context->audio_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            C64_LOG_ERROR("Audio socket error: %s", c64_get_socket_error_string(error));
            break;
        }

        if (received != C64_AUDIO_PACKET_SIZE) {
            C64_LOG_WARNING("Received incomplete audio packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                            SSIZE_T_CAST(received), C64_AUDIO_PACKET_SIZE);
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        context->last_udp_packet_time = os_gettime_ns();

        // Parse audio packet
        uint16_t seq_num = *(uint16_t *)(packet);

        // Update audio statistics
        context->audio_packets_received++;
        context->audio_bytes_received += received;

        // Streamlined audio sequence tracking (keep for important audio sync issues)
        static uint16_t last_audio_seq = 0;
        static bool first_audio = true;

        if (!first_audio && seq_num != (uint16_t)(last_audio_seq + 1)) {
            C64_LOG_WARNING("🔊 AUDIO OUT-OF-SEQUENCE: Expected %u, got %u", (uint16_t)(last_audio_seq + 1), seq_num);
        }
        last_audio_seq = seq_num;
        first_audio = false;

        // Batch process audio statistics (moved out of hot path)
        uint64_t audio_now = os_gettime_ns();
        c64_process_audio_statistics_batch(context, audio_now);

        // Push audio packet to network buffer for queuing and later processing in render thread
        if (context->network_buffer) {
            c64_network_buffer_push_audio(context->network_buffer, packet, received, audio_now);
        }
    }

    C64_LOG_DEBUG("Audio thread stopped for C64S source '%s'", obs_source_get_name(context->source));
    return NULL;
}

// Process audio packet and send to OBS for playback
void c64_process_audio_packet(struct c64_source *context, const uint8_t *audio_data, size_t data_size,
                              uint64_t timestamp_ns)
{
    if (!context || !audio_data || data_size < 2) {
        return;
    }

    // Skip the 2-byte sequence number header to get to audio samples
    const uint8_t *samples = audio_data + 2;
    size_t samples_size = data_size - 2;

    // According to C64 Ultimate spec: 192 stereo samples per packet, 16-bit signed little-endian
    // Each stereo sample = 4 bytes (2 bytes left + 2 bytes right)
    if (samples_size < 768) { // 192 * 4 = 768 bytes
        C64_LOG_WARNING("Audio packet too small: %zu bytes (expected 768)", samples_size);
        return;
    }

    // Set up OBS audio data structure
    struct obs_source_audio audio_output = {0};
    audio_output.frames = 192;            // 192 stereo samples per packet
    audio_output.samples_per_sec = 48000; // Close to C64 Ultimate's ~47.98kHz
    audio_output.format = AUDIO_FORMAT_16BIT;
    audio_output.speakers = SPEAKERS_STEREO;
    audio_output.timestamp = timestamp_ns;

    // Point to the audio data (OBS expects planar format, but we have interleaved)
    // For now, send interleaved data directly - OBS can handle it
    audio_output.data[0] = (uint8_t *)samples;

    // Send audio to OBS for playback
    obs_source_output_audio(context->source, &audio_output);

    // Also record to file if recording is enabled
    c64_record_audio_data(context, audio_data, data_size);
}
