#include <obs-module.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "c64-network.h" // Include network header first to avoid Windows header conflicts

#include <util/platform.h>
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-video.h"

void c64_send_control_command(struct c64_source *context, bool enable, uint8_t stream_id)
{
    if (strcmp(context->ip_address, "0.0.0.0") == 0) {
        C64_LOG_DEBUG("Skipping control command - no IP configured (0.0.0.0)");
        return;
    }

    socket_t sock = c64_create_tcp_socket(context->ip_address, C64_CONTROL_PORT);
    if (sock == INVALID_SOCKET_VALUE) {
        return; // Error already logged in c64_create_tcp_socket
    }

    if (enable) {
        // Get the OBS IP to send as destination
        const char *client_ip = context->obs_ip_address;

        // Ensure we have a valid OBS IP address
        if (!client_ip || strlen(client_ip) == 0) {
            C64_LOG_WARNING("No OBS IP address configured, cannot send stream start command");
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
        memcpy(&cmd[6], ip_port_str, ip_port_len);

        int cmd_len = 6 + (int)ip_port_len;
        C64_LOG_INFO("Sending start command for stream %u to %s with client destination: %s", stream_id,
                     context->ip_address, ip_port_str);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64_get_socket_error();
            C64_LOG_ERROR("Failed to send start control command: %s", c64_get_socket_error_string(error));
        } else {
            C64_LOG_DEBUG("Start control command sent successfully");
        }
    } else {
        // Disable stream command: FF3n where n is stream ID
        uint8_t cmd[4];
        cmd[0] = 0x30 + stream_id; // 0x30 for video (stream 0), 0x31 for audio (stream 1)
        cmd[1] = 0xFF;
        cmd[2] = 0x00; // No parameters
        cmd[3] = 0x00;
        int cmd_len = 4;
        C64_LOG_INFO("Sending stop command for stream %u to C64 %s", stream_id, context->ip_address);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64_get_socket_error();
            C64_LOG_ERROR("Failed to send stop control command: %s", c64_get_socket_error_string(error));
        } else {
            C64_LOG_DEBUG("Stop control command sent successfully");
        }
    }

    close(sock);
}
