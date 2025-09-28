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
		const char *client_ip = context->auto_detect_ip ? context->obs_ip_address : "192.168.1.100";
		size_t ip_len = strlen(client_ip);

		// Enable stream command with destination IP
		// Command structure: <command word LE> <param length LE> <duration LE> <IP string>
		// According to docs: FF2n where n is stream ID (0=video, 1=audio)
		uint8_t cmd[64];           // Large enough buffer for IP string
		cmd[0] = 0x20 + stream_id; // 0x20 for video (stream 0), 0x21 for audio (stream 1)
		cmd[1] = 0xFF;
		cmd[2] = (uint8_t)(2 + ip_len); // Parameter length: 2 bytes duration + IP string length
		cmd[3] = 0x00;
		cmd[4] = 0x00; // Duration: 0 = forever (little endian)
		cmd[5] = 0x00;
		memcpy(&cmd[6], client_ip, ip_len); // Copy IP string

		int cmd_len = 6 + (int)ip_len;
		C64U_LOG_INFO("Sending start command for stream %u to %s with client IP: %s", stream_id,
			      context->ip_address, client_ip);

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