#include <obs-module.h>
#include <util/platform.h>
#include <stdatomic.h>
#include "c64u-logging.h"
#include "c64u-audio.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-network.h"
#include "c64u-record.h" // For recording functions

// Audio thread function
void *audio_thread_func(void *data)
{
    struct c64u_source *context = data;
    uint8_t packet[C64U_AUDIO_PACKET_SIZE];

    C64U_LOG_DEBUG("Audio receiver thread started on port %u", context->audio_port);

    while (context->thread_active) {
        ssize_t received = recv(context->audio_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64u_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            C64U_LOG_ERROR("Audio socket error: %s", c64u_get_socket_error_string(error));
            break;
        }

        if (received != C64U_AUDIO_PACKET_SIZE) {
            C64U_LOG_WARNING("Received incomplete audio packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                             SSIZE_T_CAST(received), C64U_AUDIO_PACKET_SIZE);
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        // Use atomic store to avoid mutex contention in the hot packet path
        atomic_store_explicit(&context->last_udp_packet_time, os_gettime_ns(), memory_order_relaxed);

        // Signal the retry thread that a packet arrived to reset its wait deadline
        pthread_cond_signal(&context->retry_cond);

        // Parse audio packet
        uint16_t seq_num = *(uint16_t *)(packet);
        int16_t *audio_data = (int16_t *)(packet + C64U_AUDIO_HEADER_SIZE);

        // Technical statistics tracking - Audio
        static int audio_packet_count = 0;
        static uint64_t last_audio_log = 0;
        static uint32_t audio_bytes_period = 0;
        static uint32_t audio_packets_period = 0;
        static uint16_t last_audio_seq = 0;
        static uint32_t audio_drops = 0;
        static bool first_audio = true;

        audio_packet_count++;
        audio_bytes_period += (uint32_t)received; // Cast ssize_t to uint32_t for Windows
        audio_packets_period++;

        uint64_t audio_now = os_gettime_ns();
        if (last_audio_log == 0) {
            last_audio_log = audio_now;
            C64U_LOG_INFO("🎵 Audio statistics tracking initialized");
        }

        // Track audio packet drops
        if (!first_audio && seq_num != (uint16_t)(last_audio_seq + 1)) {
            audio_drops++;
        }
        last_audio_seq = seq_num;
        first_audio = false;

        // Log comprehensive audio statistics every 5 seconds
        uint64_t audio_time_diff = audio_now - last_audio_log;
        if (audio_time_diff >= 5000000000ULL) {
            double duration = audio_time_diff / 1000000000.0;
            double bandwidth_mbps = (audio_bytes_period * 8.0) / (duration * 1000000.0);
            double pps = audio_packets_period / duration;
            double loss_pct = audio_packets_period > 0 ? (100.0 * audio_drops) / audio_packets_period : 0.0;
            double sample_rate = audio_packets_period * 192.0 / duration; // 192 samples per packet

            C64U_LOG_INFO("🔊 AUDIO: %.0f Hz | %.2f Mbps | %.0f pps | Loss: %.1f%% | Packets: %u", sample_rate,
                          bandwidth_mbps, pps, loss_pct, audio_packet_count);

            // Reset period counters
            audio_bytes_period = 0;
            audio_packets_period = 0;
            last_audio_log = audio_now;
        }

        // Send audio to OBS (192 stereo samples = 384 16-bit values)
        struct obs_source_audio audio_frame = {0};
        audio_frame.data[0] = (uint8_t *)audio_data;
        audio_frame.frames = 192;
        audio_frame.speakers = SPEAKERS_STEREO;
        audio_frame.format = AUDIO_FORMAT_16BIT;
        audio_frame.samples_per_sec = 48000; // Will be adjusted for PAL/NTSC
        audio_frame.timestamp = os_gettime_ns();

        // Record audio data if recording is enabled
        if (context->record_video) {
            record_audio_data(context, (const uint8_t *)audio_data,
                              192 * 2 * 2); // 192 stereo samples * 2 bytes per sample
        }

        obs_source_output_audio(context->source, &audio_frame);
    }

    C64U_LOG_DEBUG("Audio thread stopped for C64U source '%s'", obs_source_get_name(context->source));
    return NULL;
}
