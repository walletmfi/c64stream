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

        // Note: Audio output now happens in c64_render() using buffered packets
        // This eliminates immediate processing and allows synchronized audio/video output
    }

    C64_LOG_DEBUG("Audio thread stopped for C64S source '%s'", obs_source_get_name(context->source));
    return NULL;
}
