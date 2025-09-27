/*
C64U Plugin for OBS
Copyright (C) 2025 Chris Gleissner

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include "plugin-support.h"
#include <graphics/graphics.h>
#include <media-io/video-io.h>
#include <media-io/audio-io.h>
#include <util/threading.h>
#include <util/platform.h>
#include <string.h>

// Platform-specific networking includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close(s) closesocket(s)
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
typedef SOCKET socket_t;
// Define ssize_t for MSVC (MinGW has it, but MSVC doesn't)
#ifndef __MINGW32__
typedef long long ssize_t;
#endif
#define INVALID_SOCKET_VALUE INVALID_SOCKET
// Format specifier for ssize_t on Windows (64-bit)
#define SSIZE_T_FORMAT "%lld"
#define SSIZE_T_CAST(x) ((long long)(x))
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
// Format specifier for ssize_t on POSIX (typically 32-bit or 64-bit depending on platform)
#define SSIZE_T_FORMAT "%zd"
#define SSIZE_T_CAST(x) (x)
#endif

#include <pthread.h>

// C64U Stream constants
#define C64U_VIDEO_PACKET_SIZE 780
#define C64U_AUDIO_PACKET_SIZE 770
#define C64U_VIDEO_HEADER_SIZE 12
#define C64U_AUDIO_HEADER_SIZE 2
#define C64U_CONTROL_PORT 64
#define C64U_DEFAULT_VIDEO_PORT 11000
#define C64U_DEFAULT_AUDIO_PORT 11001
#define C64U_DEFAULT_IP "0.0.0.0"

// Video format constants
#define C64U_PAL_WIDTH 384
#define C64U_PAL_HEIGHT 272
#define C64U_NTSC_WIDTH 384
#define C64U_NTSC_HEIGHT 240
#define C64U_PIXELS_PER_LINE 384
#define C64U_BYTES_PER_LINE 192 // 384 pixels / 2 (4-bit per pixel) - keeping original
#define C64U_LINES_PER_PACKET 4

// Logging control
static bool c64u_debug_logging = true;

#define C64U_LOG_INFO(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			obs_log(LOG_INFO, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define C64U_LOG_WARNING(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			obs_log(LOG_WARNING, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define C64U_LOG_ERROR(format, ...) \
	obs_log(LOG_ERROR, "[C64U] " format, ##__VA_ARGS__)

#define C64U_LOG_DEBUG(format, ...) \
	do { \
		if (c64u_debug_logging) { \
			obs_log(LOG_DEBUG, "[C64U] " format, ##__VA_ARGS__); \
		} \
	} while (0)
#define C64U_LINES_PER_PACKET 4

// VIC color palette (BGRA values for OBS) - converted from grab.py RGB values
static const uint32_t vic_colors[16] = {
	0xFF000000, // 0: Black
	0xFFEFEFEF, // 1: White
	0xFF342F8D, // 2: Red
	0xFFCDD46A, // 3: Cyan
	0xFFA43598, // 4: Purple/Magenta
	0xFF42B44C, // 5: Green
	0xFFB1292C, // 6: Blue
	0xFF5DEFEF, // 7: Yellow
	0xFF204E98, // 8: Orange
	0xFF00385B, // 9: Brown
	0xFF6D67D1, // 10: Light Red
	0xFF4A4A4A, // 11: Dark Grey
	0xFF7B7B7B, // 12: Mid Grey
	0xFF93EF9F, // 13: Light Green
	0xFFEF6A6D, // 14: Light Blue
	0xFFB2B2B2  // 15: Light Grey
};

struct c64u_source {
	obs_source_t *source;

	// Configuration
	char ip_address[64];
	uint32_t video_port;
	uint32_t audio_port;
	bool streaming;

	// Video data
	uint32_t width;
	uint32_t height;
	uint8_t *video_buffer;
	uint32_t *frame_buffer;
	bool frame_ready;

	// Audio data
	struct audio_output_info audio_info;

	// Network
	socket_t video_socket;
	socket_t audio_socket;
	socket_t control_socket;
	pthread_t video_thread;
	pthread_t audio_thread;
	bool thread_active;
	bool video_thread_active;
	bool audio_thread_active;

	// Synchronization
	pthread_mutex_t frame_mutex;

	// Auto-start control
	bool auto_start_attempted;
};

// Forward declarations
static void c64u_start_streaming(struct c64u_source *context);
static void c64u_stop_streaming(struct c64u_source *context);

// Network helper functions
static bool c64u_init_networking(void)
{
#ifdef _WIN32
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		C64U_LOG_ERROR("WSAStartup failed: %d", result);
		return false;
	}
	C64U_LOG_DEBUG("Windows networking initialized");
#endif
	return true;
}

static void c64u_cleanup_networking(void)
{
#ifdef _WIN32
	WSACleanup();
	C64U_LOG_DEBUG("Windows networking cleaned up");
#endif
}

static int c64u_get_socket_error(void)
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

static const char *c64u_get_socket_error_string(int error)
{
#ifdef _WIN32
	static char buffer[256];
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
		       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL);
	return buffer;
#else
	return strerror(error);
#endif
}

static socket_t create_udp_socket(uint32_t port)
{
	socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET_VALUE) {
		int error = c64u_get_socket_error();
		C64U_LOG_ERROR("Failed to create UDP socket: %s", c64u_get_socket_error_string(error));
		return INVALID_SOCKET_VALUE;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int error = c64u_get_socket_error();
		C64U_LOG_ERROR("Failed to bind UDP socket to port %u: %s", port, c64u_get_socket_error_string(error));
		close(sock);
		return INVALID_SOCKET_VALUE;
	}

	// Set socket to non-blocking
#ifdef _WIN32
	u_long mode = 1;
	if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
		int error = c64u_get_socket_error();
		C64U_LOG_WARNING("Failed to set socket non-blocking: %s", c64u_get_socket_error_string(error));
	}
#else
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	}
#endif

	C64U_LOG_DEBUG("Created UDP socket on port %u", port);
	return sock;
}

static socket_t create_tcp_socket(const char *ip, uint32_t port)
{
	if (!ip || strlen(ip) == 0) {
		C64U_LOG_ERROR("Invalid IP address provided");
		return INVALID_SOCKET_VALUE;
	}

	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET_VALUE) {
		int error = c64u_get_socket_error();
		C64U_LOG_ERROR("Failed to create TCP socket: %s", c64u_get_socket_error_string(error));
		return INVALID_SOCKET_VALUE;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
		C64U_LOG_ERROR("Invalid IP address format: %s", ip);
		close(sock);
		return INVALID_SOCKET_VALUE;
	}

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int error = c64u_get_socket_error();
		C64U_LOG_WARNING("Failed to connect to C64U at %s:%u: %s", ip, port,
				 c64u_get_socket_error_string(error));
		close(sock);
		return INVALID_SOCKET_VALUE;
	}

	C64U_LOG_DEBUG("Connected to C64U at %s:%u", ip, port);
	return sock;
}

static void send_control_command(struct c64u_source *context, bool enable, uint8_t stream_id)
{
	if (strcmp(context->ip_address, C64U_DEFAULT_IP) == 0) {
		C64U_LOG_DEBUG("Skipping control command - no IP configured");
		return;
	}

	socket_t sock = create_tcp_socket(context->ip_address, C64U_CONTROL_PORT);
	if (sock == INVALID_SOCKET_VALUE) {
		return; // Error already logged in create_tcp_socket
	}

	uint8_t cmd[6];
	int cmd_len;

	if (enable) {
		// Enable stream command: 20 FF 02+stream_id 00 00 00
		cmd[0] = 0x20;
		cmd[1] = 0xFF;
		cmd[2] = 0x02 + stream_id;
		cmd[3] = 0x00;
		cmd[4] = 0x00;
		cmd[5] = 0x00;
		cmd_len = 6;
		C64U_LOG_INFO("Sending start command for stream %u to %s", stream_id, context->ip_address);
	} else {
		// Disable stream command: 30 FF 03+stream_id 00
		cmd[0] = 0x30;
		cmd[1] = 0xFF;
		cmd[2] = 0x03 + stream_id;
		cmd[3] = 0x00;
		cmd_len = 4;
		C64U_LOG_INFO("Sending stop command for stream %u to %s", stream_id, context->ip_address);
	}

	ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
	if (sent != (ssize_t)cmd_len) {
		int error = c64u_get_socket_error();
		C64U_LOG_ERROR("Failed to send control command: %s", c64u_get_socket_error_string(error));
	} else {
		C64U_LOG_DEBUG("Control command sent successfully");
	}

	close(sock);
}

// Video thread function
static void *video_thread_func(void *data)
{
	struct c64u_source *context = data;
	uint8_t packet[C64U_VIDEO_PACKET_SIZE];
	static int packet_count = 0;

	C64U_LOG_INFO("Video receiver thread started on port %u", context->video_port);
	// Video receiver thread initialized
	C64U_LOG_INFO("🔥 VIDEO THREAD FUNCTION STARTED - Our custom statistics code will run here!");

	while (context->thread_active) {
		ssize_t received = recv(context->video_socket, (char *)packet, (int)sizeof(packet), 0);

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
			C64U_LOG_ERROR("Video socket error: %s", c64u_get_socket_error_string(error));
			break;
		}

		if (received != C64U_VIDEO_PACKET_SIZE) {
			C64U_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
					 SSIZE_T_CAST(received), C64U_VIDEO_PACKET_SIZE);
			continue;
		}

		// Debug: Count received packets
		packet_count++;
		// Technical statistics tracking - Video
		static uint64_t last_video_log = 0;
		static uint32_t video_bytes_period = 0;
		static uint32_t video_packets_period = 0;
		static uint16_t last_video_seq = 0;
		static uint32_t video_drops = 0;
		static uint32_t video_frames = 0;
		static bool first_video = true;

		// Parse packet header
		uint16_t seq_num = *(uint16_t *)(packet + 0);
		uint16_t frame_num = *(uint16_t *)(packet + 2);
		uint16_t line_num = *(uint16_t *)(packet + 4);
		uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
		uint8_t lines_per_packet = packet[8];
		uint8_t bits_per_pixel = packet[9];
		uint16_t encoding = *(uint16_t *)(packet + 10);

		UNUSED_PARAMETER(frame_num);
		UNUSED_PARAMETER(pixels_per_line);
		UNUSED_PARAMETER(bits_per_pixel);
		UNUSED_PARAMETER(encoding);

		bool last_packet = (line_num & 0x8000) != 0;
		line_num &= 0x7FFF;

		// Update video statistics
		video_bytes_period += (uint32_t)received; // Cast ssize_t to uint32_t for Windows
		video_packets_period++;

		uint64_t now = os_gettime_ns();
		if (last_video_log == 0) {
			last_video_log = now;
			C64U_LOG_INFO("� Video statistics tracking initialized");
		}

		// Track packet drops (seq_num should increment by 1)
		if (!first_video && seq_num != (uint16_t)(last_video_seq + 1)) {
			video_drops++;
		}
		last_video_seq = seq_num;
		first_video = false;

		// Count frames (last packet of frame)
		if (last_packet)
			video_frames++;

		// Log comprehensive video statistics every 5 seconds
		uint64_t time_diff = now - last_video_log;
		if (time_diff >= 5000000000ULL) {
			double duration = time_diff / 1000000000.0;
			double bandwidth_mbps = (video_bytes_period * 8.0) / (duration * 1000000.0);
			double pps = video_packets_period / duration;
			double fps = video_frames / duration;
			double loss_pct = video_packets_period > 0 ? (100.0 * video_drops) / video_packets_period : 0.0;

			C64U_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", fps,
				      bandwidth_mbps, pps, loss_pct, video_frames);

			// Reset period counters
			video_bytes_period = 0;
			video_packets_period = 0;
			video_frames = 0;
			last_video_log = now;
		}

		// Validate packet data
		if (lines_per_packet != C64U_LINES_PER_PACKET || pixels_per_line != C64U_PIXELS_PER_LINE ||
		    bits_per_pixel != 4) {
			C64U_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet,
					 pixels_per_line, bits_per_pixel);
			continue;
		}

		// Process pixel data
		if (pthread_mutex_lock(&context->frame_mutex) == 0) {
			uint8_t *pixel_data = packet + C64U_VIDEO_HEADER_SIZE;

			for (int line = 0;
			     line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
				uint32_t *dst_line = context->frame_buffer + ((line_num + line) * C64U_PIXELS_PER_LINE);
				uint8_t *src_line = pixel_data + (line * C64U_BYTES_PER_LINE);

				// Convert 4-bit VIC colors to 32-bit RGBA
				// Each byte contains 2 pixels - LOW NIBBLE FIRST (matches grab.py)
				for (int x = 0; x < C64U_BYTES_PER_LINE; x++) {
					uint8_t pixel_pair = src_line[x];
					uint8_t color1 = pixel_pair & 0x0F; // Low nibble first pixel
					uint8_t color2 = pixel_pair >> 4;   // High nibble second pixel

					// Pixel processing (debug logging removed for performance)

					dst_line[x * 2] = vic_colors[color1];
					dst_line[x * 2 + 1] = vic_colors[color2];
				}
			}

			if (last_packet) {
				context->frame_ready = true;
				// Frame completed (logged periodically in statistics)
			}

			pthread_mutex_unlock(&context->frame_mutex);
		}
	}

	C64U_LOG_INFO("Video receiver thread stopped");
	return NULL;
}

// Audio thread function
static void *audio_thread_func(void *data)
{
	struct c64u_source *context = data;
	uint8_t packet[C64U_AUDIO_PACKET_SIZE];

	C64U_LOG_INFO("Audio receiver thread started on port %u", context->audio_port);

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

			C64U_LOG_INFO("🔊 AUDIO: %.0f Hz | %.2f Mbps | %.0f pps | Loss: %.1f%% | Packets: %u",
				      sample_rate, bandwidth_mbps, pps, loss_pct, audio_packet_count);

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

		obs_source_output_audio(context->source, &audio_frame);
	}

	C64U_LOG_INFO("Audio receiver thread stopped");
	return NULL;
}

static void *c64u_create(obs_data_t *settings, obs_source_t *source)
{
	C64U_LOG_INFO("Creating C64U source");

	// C64U source creation

	// Initialize networking on first use
	static bool networking_initialized = false;
	if (!networking_initialized) {
		if (!c64u_init_networking()) {
			C64U_LOG_ERROR("Failed to initialize networking");
			return NULL;
		}
		networking_initialized = true;
	}

	struct c64u_source *context = bzalloc(sizeof(struct c64u_source));
	if (!context) {
		C64U_LOG_ERROR("Failed to allocate memory for source context");
		return NULL;
	}

	context->source = source;

	// Initialize configuration from settings
	const char *ip = obs_data_get_string(settings, "ip_address");
	strncpy(context->ip_address, ip ? ip : C64U_DEFAULT_IP, sizeof(context->ip_address) - 1);
	context->video_port = (uint32_t)obs_data_get_int(settings, "video_port");
	context->audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
	context->streaming = false;

	// Set default ports if not configured
	if (context->video_port == 0)
		context->video_port = C64U_DEFAULT_VIDEO_PORT;
	if (context->audio_port == 0)
		context->audio_port = C64U_DEFAULT_AUDIO_PORT;

	// Initialize video format (start with PAL, will be detected from stream)
	context->width = C64U_PAL_WIDTH;
	context->height = C64U_PAL_HEIGHT;

	// Allocate video buffers
	size_t frame_size = context->width * context->height * 4; // RGBA
	context->frame_buffer = bmalloc(frame_size);
	if (!context->frame_buffer) {
		C64U_LOG_ERROR("Failed to allocate video frame buffer");
		bfree(context);
		return NULL;
	}
	memset(context->frame_buffer, 0, frame_size);
	context->frame_ready = false;

	// Initialize mutex
	if (pthread_mutex_init(&context->frame_mutex, NULL) != 0) {
		C64U_LOG_ERROR("Failed to initialize frame mutex");
		bfree(context->frame_buffer);
		bfree(context);
		return NULL;
	}

	// Initialize sockets to invalid
	context->video_socket = INVALID_SOCKET_VALUE;
	context->audio_socket = INVALID_SOCKET_VALUE;
	context->control_socket = INVALID_SOCKET_VALUE;
	context->thread_active = false;
	context->video_thread_active = false;
	context->audio_thread_active = false;
	context->auto_start_attempted = false;

	C64U_LOG_INFO("C64U source created - IP: %s, Video: %u, Audio: %u", context->ip_address, context->video_port,
		      context->audio_port);

	return context;
}

static void c64u_destroy(void *data)
{
	struct c64u_source *context = data;
	if (!context)
		return;

	C64U_LOG_INFO("Destroying C64U source");

	// Stop streaming if active
	if (context->streaming) {
		C64U_LOG_DEBUG("Stopping active streaming during destruction");
		context->streaming = false;
		context->thread_active = false;

		// Send stop commands
		send_control_command(context, false, 0); // Stop video
		send_control_command(context, false, 1); // Stop audio

		// Close sockets
		if (context->video_socket != INVALID_SOCKET_VALUE) {
			close(context->video_socket);
			context->video_socket = INVALID_SOCKET_VALUE;
		}
		if (context->audio_socket != INVALID_SOCKET_VALUE) {
			close(context->audio_socket);
			context->audio_socket = INVALID_SOCKET_VALUE;
		}

		// Wait for threads to finish
		if (context->video_thread_active) {
			pthread_join(context->video_thread, NULL);
			context->video_thread_active = false;
		}
		if (context->audio_thread_active) {
			pthread_join(context->audio_thread, NULL);
			context->audio_thread_active = false;
		}
	}

	// Cleanup resources
	pthread_mutex_destroy(&context->frame_mutex);
	if (context->frame_buffer) {
		bfree(context->frame_buffer);
	}

	bfree(context);
	C64U_LOG_INFO("C64U source destroyed");
}

static void c64u_update(void *data, obs_data_t *settings)
{
	struct c64u_source *context = data;
	if (!context)
		return;

	// Update debug logging setting
	c64u_debug_logging = obs_data_get_bool(settings, "debug_logging");
	C64U_LOG_DEBUG("Debug logging %s", c64u_debug_logging ? "enabled" : "disabled");

	// Update configuration
	const char *new_ip = obs_data_get_string(settings, "ip_address");
	uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
	uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");

	// Set defaults
	if (!new_ip)
		new_ip = C64U_DEFAULT_IP;
	if (new_video_port == 0)
		new_video_port = C64U_DEFAULT_VIDEO_PORT;
	if (new_audio_port == 0)
		new_audio_port = C64U_DEFAULT_AUDIO_PORT;

	// Check if we need to restart streaming
	bool restart_needed = context->streaming &&
			      (strcmp(context->ip_address, new_ip) != 0 || context->video_port != new_video_port ||
			       context->audio_port != new_audio_port);

	if (restart_needed) {
		C64U_LOG_INFO("Restarting streaming due to configuration change");
		c64u_stop_streaming(context);
	}

	// Update configuration
	strncpy(context->ip_address, new_ip, sizeof(context->ip_address) - 1);
	context->ip_address[sizeof(context->ip_address) - 1] = '\0';
	context->video_port = new_video_port;
	context->audio_port = new_audio_port;

	if (restart_needed) {
		// Restart streaming with new settings
		c64u_start_streaming(context);
	}
}

static void c64u_start_streaming(struct c64u_source *context)
{
	if (!context || context->streaming) {
		C64U_LOG_WARNING("Cannot start streaming - invalid context or already streaming");
		return;
	}

	C64U_LOG_INFO("Starting C64U streaming to %s (video:%u, audio:%u)...", context->ip_address, context->video_port,
		      context->audio_port);

	// Create UDP sockets
	context->video_socket = create_udp_socket(context->video_port);
	context->audio_socket = create_udp_socket(context->audio_port);

	if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
		C64U_LOG_ERROR("Failed to create UDP sockets for streaming");
		if (context->video_socket != INVALID_SOCKET_VALUE) {
			close(context->video_socket);
			context->video_socket = INVALID_SOCKET_VALUE;
		}
		if (context->audio_socket != INVALID_SOCKET_VALUE) {
			close(context->audio_socket);
			context->audio_socket = INVALID_SOCKET_VALUE;
		}
		return;
	}

	// Send start commands to C64U
	send_control_command(context, true, 0); // Start video
	send_control_command(context, true, 1); // Start audio

	// Start worker threads
	context->thread_active = true;
	context->streaming = true;
	context->video_thread_active = false;
	context->audio_thread_active = false;

	if (pthread_create(&context->video_thread, NULL, video_thread_func, context) != 0) {
		C64U_LOG_ERROR("Failed to create video receiver thread");
		context->streaming = false;
		context->thread_active = false;
		close(context->video_socket);
		close(context->audio_socket);
		context->video_socket = INVALID_SOCKET_VALUE;
		context->audio_socket = INVALID_SOCKET_VALUE;
		return;
	}
	context->video_thread_active = true;

	if (pthread_create(&context->audio_thread, NULL, audio_thread_func, context) != 0) {
		C64U_LOG_ERROR("Failed to create audio receiver thread");
		context->streaming = false;
		context->thread_active = false;
		if (context->video_thread_active) {
			pthread_join(context->video_thread, NULL);
			context->video_thread_active = false;
		}
		close(context->video_socket);
		close(context->audio_socket);
		context->video_socket = INVALID_SOCKET_VALUE;
		context->audio_socket = INVALID_SOCKET_VALUE;
		return;
	}
	context->audio_thread_active = true;

	C64U_LOG_INFO("C64U streaming started successfully");
}

static void c64u_stop_streaming(struct c64u_source *context)
{
	if (!context || !context->streaming) {
		C64U_LOG_WARNING("Cannot stop streaming - invalid context or not streaming");
		return;
	}

	C64U_LOG_INFO("Stopping C64U streaming...");

	context->streaming = false;
	context->thread_active = false;

	// Send stop commands
	send_control_command(context, false, 0);
	send_control_command(context, false, 1);

	// Close sockets to wake up threads
	if (context->video_socket != INVALID_SOCKET_VALUE) {
		close(context->video_socket);
		context->video_socket = INVALID_SOCKET_VALUE;
	}
	if (context->audio_socket != INVALID_SOCKET_VALUE) {
		close(context->audio_socket);
		context->audio_socket = INVALID_SOCKET_VALUE;
	}

	// Wait for threads to finish
	if (context->video_thread_active && pthread_join(context->video_thread, NULL) != 0) {
		C64U_LOG_WARNING("Failed to join video thread");
	}
	context->video_thread_active = false;

	if (context->audio_thread_active && pthread_join(context->audio_thread, NULL) != 0) {
		C64U_LOG_WARNING("Failed to join audio thread");
	}
	context->audio_thread_active = false;

	// Reset frame state
	if (pthread_mutex_lock(&context->frame_mutex) == 0) {
		context->frame_ready = false;
		pthread_mutex_unlock(&context->frame_mutex);
	}

	C64U_LOG_INFO("C64U streaming stopped");
}

static void c64u_render(void *data, gs_effect_t *effect)
{
	struct c64u_source *context = data;
	UNUSED_PARAMETER(effect);

	// Auto-start streaming on first render (when source is truly active)
	if (!context->auto_start_attempted) {
		context->auto_start_attempted = true;
		if (!context->streaming) {
			C64U_LOG_INFO("🚀 Auto-starting C64U streaming (source now active)...");
			c64u_start_streaming(context);
		}
	}

	// Check if we have streaming data and frame ready
	if (context->streaming && context->frame_ready && context->frame_buffer) {
		// Render actual C64U video frame from frame buffer

		// Lock the frame buffer to ensure thread safety
		if (pthread_mutex_lock(&context->frame_mutex) == 0) {
			// Create texture from frame buffer data
			gs_texture_t *texture = gs_texture_create(context->width, context->height, GS_RGBA, 1,
								  (const uint8_t **)&context->frame_buffer, 0);

			if (texture) {
				// Use default effect for texture rendering
				gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
				if (default_effect) {
					gs_eparam_t *image_param = gs_effect_get_param_by_name(default_effect, "image");
					if (image_param) {
						gs_effect_set_texture(image_param, texture);

						gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
						if (tech) {
							gs_technique_begin(tech);
							gs_technique_begin_pass(tech, 0);
							gs_draw_sprite(texture, 0, context->width, context->height);
							gs_technique_end_pass(tech);
							gs_technique_end(tech);
						}
					}
				}

				// Clean up texture
				gs_texture_destroy(texture);
			}

			pthread_mutex_unlock(&context->frame_mutex);
		}
	} else {
		// Show colored background to indicate plugin state
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

		if (solid && color) {
			gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
			if (tech) {
				gs_technique_begin(tech);
				gs_technique_begin_pass(tech, 0);

				if (context->streaming) {
					// Show yellow if streaming but no frame ready yet
					struct vec4 yellow = {0.8f, 0.8f, 0.2f, 1.0f};
					gs_effect_set_vec4(color, &yellow);
				} else {
					// Show dark blue to indicate plugin loaded but no streaming
					struct vec4 dark_blue = {0.1f, 0.2f, 0.4f, 1.0f};
					gs_effect_set_vec4(color, &dark_blue);
				}

				gs_draw_sprite(NULL, 0, context->width, context->height);

				gs_technique_end_pass(tech);
				gs_technique_end(tech);
			}
		}
	}
}

static uint32_t c64u_get_width(void *data)
{
	struct c64u_source *context = data;
	return context->width;
}

static uint32_t c64u_get_height(void *data)
{
	struct c64u_source *context = data;
	return context->height;
}

static const char *c64u_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "C64U Display";
}

static bool start_stop_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct c64u_source *context = data;

	if (context->streaming) {
		c64u_stop_streaming(context);
		obs_property_set_description(property, "Start Streaming");
	} else {
		c64u_start_streaming(context);
		obs_property_set_description(property, "Stop Streaming");
	}

	return true; // Refresh properties
}

static obs_properties_t *c64u_properties(void *data)
{
	// C64U properties setup

	struct c64u_source *context = data;
	obs_properties_t *props = obs_properties_create();

	// Debug logging toggle
	obs_property_t *debug_prop = obs_properties_add_bool(props, "debug_logging", "Enable Debug Logging");
	obs_property_set_long_description(debug_prop, "Enable detailed logging for debugging C64U connection issues");

	// IP Address
	obs_property_t *ip_prop = obs_properties_add_text(props, "ip_address", "C64U IP Address", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(
		ip_prop, "IP address of the C64 Ultimate device. Leave as 0.0.0.0 to skip control commands.");

	// Video Port
	obs_property_t *video_port_prop = obs_properties_add_int(props, "video_port", "Video Port", 1024, 65535, 1);
	obs_property_set_long_description(video_port_prop, "UDP port for video stream (default: 11000)");

	// Audio Port
	obs_property_t *audio_port_prop = obs_properties_add_int(props, "audio_port", "Audio Port", 1024, 65535, 1);
	obs_property_set_long_description(audio_port_prop, "UDP port for audio stream (default: 11001)");

	// Start/Stop button
	obs_property_t *button_prop = obs_properties_add_button(
		props, "start_stop", context && context->streaming ? "Stop Streaming" : "Start Streaming",
		start_stop_clicked);
	obs_property_set_long_description(button_prop, "Start or stop streaming from the C64 Ultimate device");

	return props;
}

static void c64u_defaults(obs_data_t *settings)
{
	// C64U defaults initialization

	obs_data_set_default_bool(settings, "debug_logging", true);
	obs_data_set_default_string(settings, "ip_address", C64U_DEFAULT_IP);
	obs_data_set_default_int(settings, "video_port", C64U_DEFAULT_VIDEO_PORT);
	obs_data_set_default_int(settings, "audio_port", C64U_DEFAULT_AUDIO_PORT);
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	C64U_LOG_INFO("Loading C64U plugin (version %s)", PLUGIN_VERSION);

	// DEBUG: This will always be hit when the plugin loads
	// Module loading

	struct obs_source_info c64u_info = {.id = "c64u_source",
					    .type = OBS_SOURCE_TYPE_INPUT,
					    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO,
					    .get_name = c64u_get_name,
					    .create = c64u_create,
					    .destroy = c64u_destroy,
					    .update = c64u_update,
					    .get_defaults = c64u_defaults,
					    .video_render = c64u_render,
					    .get_properties = c64u_properties,
					    .get_width = c64u_get_width,
					    .get_height = c64u_get_height};

	obs_register_source(&c64u_info);
	C64U_LOG_INFO("C64U plugin loaded successfully");
	return true;
}

void obs_module_unload(void)
{
	C64U_LOG_INFO("Unloading C64U plugin");
	c64u_cleanup_networking();
}
