#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h> // For atomic operations
#include <inttypes.h>       // For PRIu64, PRId64 format specifiers
#include "c64-network.h"    // Include network header first to avoid Windows header conflicts

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

    while (os_atomic_load_bool(&context->thread_active)) {
        // Check socket validity before each recv call (prevents Windows WSAENOTSOCK errors)
        if (context->audio_socket == INVALID_SOCKET_VALUE) {
            os_sleep_ms(10); // Wait a bit before checking again
            continue;
        }

        ssize_t received = recv(context->audio_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Audio socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("Audio socket shutdown (WSAESHUTDOWN) - exiting receiver thread for reconnection");
                break; // Socket was shutdown, exit gracefully
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Audio socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("Audio socket error: %s (error code: %d)", c64_get_socket_error_string(error), error);
            break;
        }

        if (received != C64_AUDIO_PACKET_SIZE) {
            // Small packets (2-4 bytes) are normal during stream startup/buffer changes
            // Log as debug to avoid confusing users with normal control/startup packets
            static uint64_t last_incomplete_log_time = 0;
            uint64_t now = os_gettime_ns();
            if (now - last_incomplete_log_time >= 2000000000ULL) { // Throttle to every 2 seconds
                if (received <= 4) {
                    C64_LOG_DEBUG("Audio startup/control packets: " SSIZE_T_FORMAT
                                  " bytes (normal during initialization)",
                                  SSIZE_T_CAST(received));
                } else {
                    C64_LOG_WARNING("Received incomplete audio packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                                    SSIZE_T_CAST(received), C64_AUDIO_PACKET_SIZE);
                }
                last_incomplete_log_time = now;
            }
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        uint64_t packet_time = os_gettime_ns();
        context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
        context->last_audio_packet_time = packet_time;

        // Update audio statistics
        os_atomic_set_long(&context->audio_packets_received, os_atomic_load_long(&context->audio_packets_received) + 1);
        os_atomic_set_long(&context->audio_bytes_received,
                           os_atomic_load_long(&context->audio_bytes_received) + (long)received);

        // Simple approach: just process packets as they arrive, no sequence warnings

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

// Audio timestamp validation to prevent OBS TS_SMOOTHING_THRESHOLD warnings
static void validate_audio_timestamp_progression(uint64_t current_timestamp)
{
    static uint64_t last_audio_timestamp = 0;
    if (last_audio_timestamp > 0) {
        int64_t timestamp_delta = (int64_t)(current_timestamp - last_audio_timestamp);
        // Expected delta is ~4ms (4,000,000 ns), warn if significantly off
        if (timestamp_delta < 2000000 || timestamp_delta > 6000000) {
            C64_LOG_DEBUG("Audio timestamp jump detected: delta=%" PRId64 "ns (expected ~4000000ns)", timestamp_delta);
        }
    }
    last_audio_timestamp = current_timestamp;
}

// Generate monotonic audio timestamps at exactly 4ms intervals
static uint64_t generate_monotonic_audio_timestamp(void)
{
    // Audio packets must have exactly 4ms intervals (192 samples at 48kHz = 4000000ns)
    // This creates synthetic timestamps independent of network jitter
    static uint64_t audio_base_time = 0;
    static uint64_t audio_packet_count = 0;

    // Initialize on first call using system time
    if (audio_base_time == 0) {
        audio_base_time = os_gettime_ns();
        C64_LOG_DEBUG("Audio synthetic timestamps initialized: base=%" PRIu64, audio_base_time);
    }

    // Calculate current timestamp: base + (packet_count * 4ms)
    uint64_t current_timestamp = audio_base_time + (audio_packet_count * 4000000ULL);
    audio_packet_count++;

    // Debug logging every 100 packets to verify progression
    static int debug_count = 0;
    if ((++debug_count % 100) == 0) {
        C64_LOG_DEBUG("Audio synthetic TS: count=%" PRIu64 ", timestamp=%" PRIu64 " (+%dms from base)",
                      audio_packet_count - 1, current_timestamp,
                      (int)((current_timestamp - audio_base_time) / 1000000));
    }

    return current_timestamp;
}

// Process audio packet and send to OBS for playback
void c64_process_audio_packet(struct c64_source *context, const uint8_t *audio_data, size_t data_size,
                              uint64_t timestamp_ns)
{
    if (!context || !audio_data || data_size < 2) {
        return;
    }

    // Use the original packet timestamp to maintain sync with video through network buffer

    // Skip the 2-byte sequence number header to get to audio samples
    const uint8_t *samples = audio_data + 2;
    size_t samples_size = data_size - 2;

    // According to C64 Ultimate spec: 192 stereo samples per packet, 16-bit signed little-endian
    // Each stereo sample = 4 bytes (2 bytes left + 2 bytes right)
    if (samples_size < 768) { // 192 * 4 = 768 bytes
        C64_LOG_WARNING("Audio packet too small: %zu bytes (expected 768)", samples_size);
        return;
    }

    // Generate synthetic audio timestamp with exactly 4ms intervals
    // This ensures monotonic progression independent of network jitter
    uint64_t audio_timestamp = generate_monotonic_audio_timestamp();

    // Validate timestamp progression for debugging
    validate_audio_timestamp_progression(audio_timestamp);

    // Set up OBS audio data structure
    struct obs_source_audio audio_output = {0};
    audio_output.frames = 192;            // 192 stereo samples per packet
    audio_output.samples_per_sec = 48000; // Close to C64 Ultimate's ~47.98kHz
    audio_output.format = AUDIO_FORMAT_16BIT;
    audio_output.speakers = SPEAKERS_STEREO;
    audio_output.timestamp = audio_timestamp; // Use original packet timestamp from network buffer

    // Point to the audio data (OBS expects planar format, but we have interleaved)
    // For now, send interleaved data directly - OBS can handle it
    audio_output.data[0] = (uint8_t *)samples;

    // Send audio to OBS for playback
    obs_source_output_audio(context->source, &audio_output);

    // Very rare spot checks for audio timestamp debugging (every 10 minutes)
    static int audio_timestamp_debug_count = 0;
    static uint64_t last_audio_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++audio_timestamp_debug_count % 50000) == 0 ||
        (now - last_audio_log_time >= 600000000000ULL)) { // Every 50k packets OR 10 minutes
        C64_LOG_DEBUG("🎵 AUDIO SPOT CHECK: audio_ts=%" PRIu64 ", packet_ts=%" PRIu64 ", delta=%+" PRId64
                      " (processed: %d)",
                      audio_timestamp, timestamp_ns, (int64_t)(audio_timestamp - timestamp_ns),
                      audio_timestamp_debug_count);
        last_audio_log_time = now;
    }

    // Also record to file if recording is enabled (use processed samples, not raw packet)
    c64_record_audio_data(context, samples, samples_size);
}
