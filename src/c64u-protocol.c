#include <obs-module.h>
#include <string.h>
#include <time.h>
#include <util/platform.h>
#include "c64u-logging.h"
#include "c64u-protocol.h"
#include "c64u-network.h"
#include "c64u-source.h"
#include "c64u-types.h"

void send_control_command(struct c64u_source *context, bool enable, uint8_t stream_id)
{
    if (strcmp(context->ip_address, C64U_DEFAULT_IP) == 0) {
        C64U_LOG_DEBUG("Skipping control command - no IP configured");
        return;
    }

    socket_t sock = create_tcp_socket(context->ip_address, C64U_CONTROL_PORT);
    if (sock == INVALID_SOCKET_VALUE) {
        return; // Error already logged in create_tcp_socket
    }

    if (enable) {
        // Get the OBS IP to send as destination
        const char *client_ip = context->obs_ip_address;

        // Ensure we have a valid OBS IP address
        if (!client_ip || strlen(client_ip) == 0) {
            C64U_LOG_WARNING("No OBS IP address configured, cannot send stream start command");
            close(sock);
            return;
        }

        // Create IP:PORT string for the destination
        char ip_port_str[128]; // Larger buffer to avoid truncation warnings
        uint32_t port = (stream_id == 0) ? context->video_port : context->audio_port;
        snprintf(ip_port_str, sizeof(ip_port_str), "%s:%u", client_ip, port);
        size_t ip_port_len = strlen(ip_port_str);

        // Enable stream command with destination IP:PORT
        // Command structure: <command word LE> <param length LE> <duration LE> <IP:PORT string>
        // According to docs: FF2n where n is stream ID (0=video, 1=audio)
        uint8_t cmd[140];          // Large enough buffer for IP:PORT string (128 + header bytes)
        cmd[0] = 0x20 + stream_id; // 0x20 for video (stream 0), 0x21 for audio (stream 1)
        cmd[1] = 0xFF;
        cmd[2] = (uint8_t)(2 + ip_port_len); // Parameter length: 2 bytes duration + IP:PORT string length
        cmd[3] = 0x00;
        cmd[4] = 0x00; // Duration: 0 = forever (little endian)
        cmd[5] = 0x00;
        memcpy(&cmd[6], ip_port_str, ip_port_len); // Copy IP:PORT string

        int cmd_len = 6 + (int)ip_port_len;
        C64U_LOG_INFO("Sending start command for stream %u to %s with client destination: %s", stream_id,
                      context->ip_address, ip_port_str);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64u_get_socket_error();
            C64U_LOG_ERROR("Failed to send start control command: %s", c64u_get_socket_error_string(error));
        } else {
            C64U_LOG_DEBUG("Start control command sent successfully");
        }
    } else {
        // Disable stream command: FF3n where n is stream ID
        uint8_t cmd[4];
        cmd[0] = 0x30 + stream_id; // 0x30 for video (stream 0), 0x31 for audio (stream 1)
        cmd[1] = 0xFF;
        cmd[2] = 0x00; // No parameters
        cmd[3] = 0x00;
        int cmd_len = 4;
        C64U_LOG_INFO("Sending stop command for stream %u to C64 %s", stream_id, context->ip_address);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64u_get_socket_error();
            C64U_LOG_ERROR("Failed to send stop control command: %s", c64u_get_socket_error_string(error));
        } else {
            C64U_LOG_DEBUG("Stop control command sent successfully");
        }
    }

    close(sock);
}

void send_control_command_async(struct c64u_source *context, bool enable, uint8_t stream_id)
{
    (void)enable;    // Mark as used - async retry always sends start commands
    (void)stream_id; // Mark as used - async retry sends both video and audio

    pthread_mutex_lock(&context->retry_mutex);
    context->needs_retry = true;
    pthread_cond_signal(&context->retry_cond);
    pthread_mutex_unlock(&context->retry_mutex);
}

void init_async_retry_system(struct c64u_source *context)
{
    pthread_mutex_init(&context->retry_mutex, NULL);
    pthread_cond_init(&context->retry_cond, NULL);
    context->retry_thread_active = false;
    context->needs_retry = false;
    context->retry_count = 0;
    context->retry_shutdown = false;
    context->last_udp_packet_time = os_gettime_ns();

    if (pthread_create(&context->retry_thread, NULL, async_retry_thread, context) == 0) {
        context->retry_thread_active = true;
        C64U_LOG_INFO("Async retry system initialized");
    } else {
        C64U_LOG_ERROR("Failed to create async retry thread");
    }
}

void shutdown_async_retry_system(struct c64u_source *context)
{
    if (context->retry_thread_active) {
        pthread_mutex_lock(&context->retry_mutex);
        context->retry_shutdown = true;
        pthread_cond_signal(&context->retry_cond);
        pthread_mutex_unlock(&context->retry_mutex);

        pthread_join(context->retry_thread, NULL);
        context->retry_thread_active = false;
        C64U_LOG_INFO("Async retry system shutdown");
    }

    pthread_mutex_destroy(&context->retry_mutex);
    pthread_cond_destroy(&context->retry_cond);
}

void *async_retry_thread(void *data)
{
    struct c64u_source *context = (struct c64u_source *)data;

    C64U_LOG_INFO("Async retry thread started - will continuously retry every 500ms");

    while (true) {
        pthread_mutex_lock(&context->retry_mutex);

        // Check for shutdown first
        if (context->retry_shutdown) {
            pthread_mutex_unlock(&context->retry_mutex);
            break;
        }

        // Check if we need to retry based on timeout or explicit request
        uint64_t now = os_gettime_ns();
        uint64_t time_since_udp = now - context->last_udp_packet_time;
        bool should_retry = context->needs_retry || (time_since_udp > 500000000ULL); // 500ms timeout

        if (should_retry) {
            context->needs_retry = false;
            pthread_mutex_unlock(&context->retry_mutex);

            // Skip if no IP configured
            if (strcmp(context->ip_address, "0.0.0.0") == 0) {
                C64U_LOG_DEBUG("Async retry skipped - no C64U IP configured");
                os_sleep_ms(1000); // Wait longer when no IP configured
                continue;
            }

            // Perform async retry - start streaming if not active, otherwise send commands
            C64U_LOG_INFO("Async retry attempt %u - %s", context->retry_count,
                          context->streaming ? "sending start commands" : "starting streaming");

            bool tcp_success = false;
            if (!context->streaming) {
                // Not streaming - need to create UDP sockets and threads
                c64u_start_streaming(context);
                tcp_success = true; // c64u_start_streaming handles TCP commands internally
            } else {
                // Already streaming - send start commands and check for success
                socket_t test_sock = create_tcp_socket(context->ip_address, C64U_CONTROL_PORT);
                if (test_sock != INVALID_SOCKET_VALUE) {
                    close(test_sock);                       // Just testing connectivity
                    send_control_command(context, true, 0); // Video
                    send_control_command(context, true, 1); // Audio
                    tcp_success = true;
                    context->consecutive_failures = 0; // Reset failure counter on success
                } else {
                    tcp_success = false;
                    context->consecutive_failures++;
                }
            }

            context->retry_count++;

            // Adaptive retry delay based on consecutive failures
            uint32_t retry_delay = 200; // Base 200ms delay
            if (context->consecutive_failures > 0 && !tcp_success) {
                // Progressive backoff: 200ms -> 500ms -> 1s -> 2s -> 3s (max)
                retry_delay = 200 + (context->consecutive_failures * 300);
                if (retry_delay > 3000)
                    retry_delay = 3000; // Cap at 3 seconds
                C64U_LOG_INFO("TCP connection failed (%u consecutive), using %ums retry delay",
                              context->consecutive_failures, retry_delay);
            }
            os_sleep_ms(retry_delay);
        } else {
            // Wait up to 100ms for signal, then check timeout again
            // Use absolute time for better cross-platform compatibility
            struct timespec timeout_spec;
            uint64_t now_ns = os_gettime_ns();
            timeout_spec.tv_sec = (now_ns + 100000000ULL) / 1000000000ULL;  // Convert to seconds
            timeout_spec.tv_nsec = (now_ns + 100000000ULL) % 1000000000ULL; // Remaining nanoseconds
            pthread_cond_timedwait(&context->retry_cond, &context->retry_mutex, &timeout_spec);
            pthread_mutex_unlock(&context->retry_mutex);
        }
    }

    C64U_LOG_INFO("Async retry thread ended");
    return NULL;
}
