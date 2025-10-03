#include <obs-module.h>
#include <string.h>
#include <util/platform.h>
#include "c64u-logging.h"
#include "c64u-protocol.h"
#include "c64u-network.h"
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

    C64U_LOG_INFO("Async retry thread started");

    while (true) {
        pthread_mutex_lock(&context->retry_mutex);

        // Wait for retry signal or shutdown
        while (!context->needs_retry && !context->retry_shutdown) {
            pthread_cond_wait(&context->retry_cond, &context->retry_mutex);
        }

        if (context->retry_shutdown) {
            pthread_mutex_unlock(&context->retry_mutex);
            break;
        }

        if (context->needs_retry) {
            context->needs_retry = false;
            pthread_mutex_unlock(&context->retry_mutex);

            // Perform async retry - send start commands for both video and audio
            C64U_LOG_INFO("Async retry attempt %u - sending start commands", context->retry_count);
            send_control_command(context, true, 0); // Video
            send_control_command(context, true, 1); // Audio
            context->retry_count++;

            // Wait 1 second before allowing next retry
            os_sleep_ms(1000);
        } else {
            pthread_mutex_unlock(&context->retry_mutex);
        }
    }

    C64U_LOG_INFO("Async retry thread ended");
    return NULL;
}
