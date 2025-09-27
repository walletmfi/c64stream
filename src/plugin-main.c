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
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
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
#ifdef __APPLE__
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#elif __linux__
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>
#endif
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

// Frame assembly constants
#define C64U_MAX_PACKETS_PER_FRAME 68           // PAL: 272 lines ÷ 4 lines/packet = 68 packets
#define C64U_FRAME_TIMEOUT_MS 100               // Timeout for incomplete frames
#define C64U_PAL_FRAME_INTERVAL_NS 20000000ULL  // 20ms for 50Hz PAL
#define C64U_NTSC_FRAME_INTERVAL_NS 16666667ULL // 16.67ms for 60Hz NTSC

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

// Frame packet structure for reordering
struct frame_packet {
	uint16_t line_num;
	uint8_t lines_per_packet;
	uint8_t packet_data[C64U_VIDEO_PACKET_SIZE - C64U_VIDEO_HEADER_SIZE];
	bool received;
};

// Frame assembly structure
struct frame_assembly {
	uint16_t frame_num;
	uint16_t expected_packets;
	uint16_t received_packets;
	struct frame_packet packets[C64U_MAX_PACKETS_PER_FRAME];
	bool complete;
	uint64_t start_time;
};

struct c64u_source {
	obs_source_t *source;

	// Configuration
	char ip_address[64];     // C64 IP Address (C64 Ultimate device)
	char obs_ip_address[64]; // OBS IP Address (this machine)
	bool auto_detect_ip;
	bool initial_ip_detected; // Flag to track if initial IP detection was done
	uint32_t video_port;
	uint32_t audio_port;
	bool streaming;

	// Video data
	uint32_t width;
	uint32_t height;
	uint8_t *video_buffer;

	// Double buffering for smooth video
	uint32_t *frame_buffer_front; // For rendering (OBS thread)
	uint32_t *frame_buffer_back;  // For UDP assembly (video thread)
	bool frame_ready;
	bool buffer_swap_pending;

	// Frame assembly and packet reordering
	struct frame_assembly current_frame;
	uint16_t last_completed_frame;
	uint32_t frame_drops;
	uint32_t packet_drops;

	// Frame diagnostic counters (Stats for Nerds style)
	uint32_t frames_expected;
	uint32_t frames_captured;
	uint32_t frames_delivered_to_obs;
	uint32_t frames_completed;
	uint32_t buffer_swaps;
	uint64_t last_capture_time;
	uint64_t total_capture_latency;
	uint64_t total_pipeline_latency;

	// Dynamic video format detection
	uint32_t detected_frame_height;
	bool format_detected;
	double expected_fps;

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
	pthread_mutex_t assembly_mutex;

	// Frame timing
	uint64_t last_frame_time;
	uint64_t frame_interval_ns; // Target frame interval (20ms for 50Hz PAL)

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

// Function to detect local machine's IP address
static bool c64u_detect_local_ip(char *ip_buffer, size_t buffer_size)
{
	if (!ip_buffer || buffer_size < 16) {
		return false;
	}

	// Initialize buffer
	strncpy(ip_buffer, "127.0.0.1", buffer_size - 1);
	ip_buffer[buffer_size - 1] = '\0';

#ifdef _WIN32
	// Windows implementation using GetAdaptersInfo
	PIP_ADAPTER_INFO adapter_info = NULL;
	ULONG out_buf_len = 0;
	DWORD ret_val = 0;

	// Get buffer size needed
	ret_val = GetAdaptersInfo(adapter_info, &out_buf_len);
	if (ret_val == ERROR_BUFFER_OVERFLOW) {
		adapter_info = (IP_ADAPTER_INFO *)malloc(out_buf_len);
		if (adapter_info != NULL) {
			ret_val = GetAdaptersInfo(adapter_info, &out_buf_len);
			if (ret_val == NO_ERROR) {
				PIP_ADAPTER_INFO adapter = adapter_info;
				while (adapter) {
					// Skip loopback and inactive adapters
					if (adapter->Type != MIB_IF_TYPE_LOOPBACK &&
					    strcmp(adapter->IpAddressList.IpAddress.String, "0.0.0.0") != 0) {
						strncpy(ip_buffer, adapter->IpAddressList.IpAddress.String,
							buffer_size - 1);
						ip_buffer[buffer_size - 1] = '\0';
						C64U_LOG_INFO("Detected Windows IP address: %s (adapter: %s)",
							      ip_buffer, adapter->AdapterName);
						free(adapter_info);
						return true;
					}
					adapter = adapter->Next;
				}
			}
			free(adapter_info);
		}
	}
	C64U_LOG_WARNING("Failed to detect Windows IP address, using fallback");
	return false;

#elif defined(__APPLE__) || defined(__linux__)
	// Unix/Linux/macOS implementation using getifaddrs
	struct ifaddrs *ifaddrs_ptr, *ifa;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddrs_ptr) == -1) {
		C64U_LOG_WARNING("getifaddrs failed: %s", strerror(errno));
		return false;
	}

	for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
			continue;
		}

		// Skip loopback interface
		if (strcmp(ifa->ifa_name, "lo") == 0 || strcmp(ifa->ifa_name, "lo0") == 0) {
			continue;
		}

		int result = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0,
					 NI_NUMERICHOST);
		if (result != 0) {
			continue;
		}

		// Skip if it's a localhost address
		if (strcmp(host, "127.0.0.1") == 0) {
			continue;
		}

		// We found a valid IP address
		strncpy(ip_buffer, host, buffer_size - 1);
		ip_buffer[buffer_size - 1] = '\0';
		C64U_LOG_INFO("Detected %s IP address: %s (interface: %s)",
#ifdef __APPLE__
			      "macOS",
#else
			      "Linux",
#endif
			      ip_buffer, ifa->ifa_name);
		freeifaddrs(ifaddrs_ptr);
		return true;
	}

	freeifaddrs(ifaddrs_ptr);
	C64U_LOG_WARNING("No suitable network interface found, using fallback");
	return false;

#else
	// Fallback for other platforms
	C64U_LOG_WARNING("IP detection not implemented for this platform, using fallback");
	return false;
#endif
}

static void c64u_cleanup_networking(void)
{
#ifdef _WIN32
	WSACleanup();
	C64U_LOG_DEBUG("Windows networking cleaned up");
#endif
}

// Helper functions for frame assembly
static void init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
	memset(frame, 0, sizeof(struct frame_assembly));
	frame->frame_num = frame_num;
	frame->start_time = os_gettime_ns();
}

static bool is_frame_complete(struct frame_assembly *frame)
{
	return frame->received_packets > 0 && frame->received_packets == frame->expected_packets;
}

static bool is_frame_timeout(struct frame_assembly *frame)
{
	uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
	return elapsed > C64U_FRAME_TIMEOUT_MS;
}

static void swap_frame_buffers(struct c64u_source *context)
{
	// Atomically swap front and back buffers
	uint32_t *temp = context->frame_buffer_front;
	context->frame_buffer_front = context->frame_buffer_back;
	context->frame_buffer_back = temp;
	context->frame_ready = true;
	context->buffer_swap_pending = false;
}

static void assemble_frame_to_buffer(struct c64u_source *context, struct frame_assembly *frame)
{
	// Assemble complete frame into back buffer
	for (int i = 0; i < C64U_MAX_PACKETS_PER_FRAME; i++) {
		struct frame_packet *packet = &frame->packets[i];
		if (!packet->received)
			continue;

		uint16_t line_num = packet->line_num;
		uint8_t lines_per_packet = packet->lines_per_packet;

		for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height;
		     line++) {
			uint32_t *dst_line = context->frame_buffer_back + ((line_num + line) * C64U_PIXELS_PER_LINE);
			uint8_t *src_line = packet->packet_data + (line * C64U_BYTES_PER_LINE);

			// Convert 4-bit VIC colors to 32-bit RGBA
			for (int x = 0; x < C64U_BYTES_PER_LINE; x++) {
				uint8_t pixel_pair = src_line[x];
				uint8_t color1 = pixel_pair & 0x0F;
				uint8_t color2 = pixel_pair >> 4;

				dst_line[x * 2] = vic_colors[color1];
				dst_line[x * 2 + 1] = vic_colors[color2];
			}
		}
	}
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

		// NOTE: Frame counting is now done only in frame assembly completion logic
		// Do not count frames here based on last_packet flag as it creates duplicate counting

		// Log comprehensive video statistics every 5 seconds
		uint64_t time_diff = now - last_video_log;
		if (time_diff >= 5000000000ULL) {
			double duration = time_diff / 1000000000.0;
			double bandwidth_mbps = (video_bytes_period * 8.0) / (duration * 1000000.0);
			double pps = video_packets_period / duration;
			double fps = video_frames / duration;
			double loss_pct = video_packets_period > 0 ? (100.0 * video_drops) / video_packets_period : 0.0;

			// Calculate frame delivery metrics (Stats for Nerds style)
			double expected_fps = context->format_detected ? context->expected_fps
								       : 50.0; // Default to PAL if not detected yet
			double frame_delivery_rate = context->frames_delivered_to_obs / duration;
			double frame_completion_rate = context->frames_completed / duration;
			double capture_drop_pct =
				context->frames_expected > 0
					? (100.0 * (context->frames_expected - context->frames_captured)) /
						  context->frames_expected
					: 0.0;
			double delivery_drop_pct =
				context->frames_completed > 0
					? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) /
						  context->frames_completed
					: 0.0;
			double avg_pipeline_latency = context->frames_delivered_to_obs > 0
							      ? context->total_pipeline_latency /
									(context->frames_delivered_to_obs * 1000000.0)
							      : 0.0; // Convert to ms

			C64U_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", fps,
				      bandwidth_mbps, pps, loss_pct, video_frames);
			C64U_LOG_INFO(
				"🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
				expected_fps, context->frames_captured / duration, frame_delivery_rate,
				frame_completion_rate);
			C64U_LOG_INFO(
				"📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms | Buffer swaps %u",
				capture_drop_pct, delivery_drop_pct, avg_pipeline_latency, context->buffer_swaps);

			// Reset period counters
			video_bytes_period = 0;
			video_packets_period = 0;
			video_frames = 0;
			// Reset diagnostic counters
			context->frames_expected = 0;
			context->frames_captured = 0;
			context->frames_delivered_to_obs = 0;
			context->frames_completed = 0;
			context->buffer_swaps = 0;
			context->total_pipeline_latency = 0;
			last_video_log = now;
		}

		// Validate packet data
		if (lines_per_packet != C64U_LINES_PER_PACKET || pixels_per_line != C64U_PIXELS_PER_LINE ||
		    bits_per_pixel != 4) {
			C64U_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet,
					 pixels_per_line, bits_per_pixel);
			continue;
		}

		// Process packet with frame assembly and double buffering
		if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
			// Track frame capture timing for diagnostics (per-frame, not per-packet)
			uint64_t capture_time = os_gettime_ns();

			// Check if this is a new frame
			if (context->current_frame.frame_num != frame_num) {
				// Count expected and captured frames only on new frame start
				if (context->last_capture_time > 0) {
					context->frames_expected++;
				}
				context->frames_captured++;
				context->last_capture_time = capture_time;
				// Complete previous frame if it exists and is reasonably complete
				if (context->current_frame.received_packets > 0) {
					if (is_frame_complete(&context->current_frame) ||
					    is_frame_timeout(&context->current_frame)) {
						if (is_frame_complete(&context->current_frame)) {
							// Assemble complete frame and swap buffers (only if not already completed)
							if (pthread_mutex_lock(&context->frame_mutex) == 0) {
								if (context->last_completed_frame !=
								    context->current_frame.frame_num) {
									assemble_frame_to_buffer(
										context, &context->current_frame);
									swap_frame_buffers(context);
									context->last_completed_frame =
										context->current_frame.frame_num;
									// Track diagnostics consistently
									context->frames_completed++;
									context->buffer_swaps++;
									context->frames_delivered_to_obs++;
									context->total_pipeline_latency +=
										(os_gettime_ns() - capture_time);
									// video_frames++ removed - counted in main completion section
								}
								pthread_mutex_unlock(&context->frame_mutex);
							}
						} else {
							// Frame timeout - log drop and continue
							context->frame_drops++;
						}
					}
				}

				// Start new frame
				init_frame_assembly(&context->current_frame, frame_num);
			}

			// Add packet to current frame (calculate packet index from line number)
			uint16_t packet_index = line_num / lines_per_packet;
			if (packet_index < C64U_MAX_PACKETS_PER_FRAME) {
				struct frame_packet *fp = &context->current_frame.packets[packet_index];
				if (!fp->received) {
					fp->line_num = line_num;
					fp->lines_per_packet = lines_per_packet;
					fp->received = true;
					memcpy(fp->packet_data, packet + C64U_VIDEO_HEADER_SIZE,
					       C64U_VIDEO_PACKET_SIZE - C64U_VIDEO_HEADER_SIZE);
					context->current_frame.received_packets++;
				}

				// Update expected packet count and detect video format based on last packet
				if (last_packet && context->current_frame.expected_packets == 0) {
					context->current_frame.expected_packets = packet_index + 1;

					// Detect PAL vs NTSC format from frame height
					uint32_t frame_height = line_num + lines_per_packet;
					if (!context->format_detected ||
					    context->detected_frame_height != frame_height) {
						context->detected_frame_height = frame_height;
						context->format_detected = true;

						// Calculate expected FPS based on detected format
						if (frame_height == C64U_PAL_HEIGHT) {
							context->expected_fps = 50.0; // PAL: 50 Hz
							C64U_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.0f Hz",
								      frame_height, context->expected_fps);
						} else if (frame_height == C64U_NTSC_HEIGHT) {
							context->expected_fps = 60.0; // NTSC: 60 Hz
							C64U_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.0f Hz",
								      frame_height, context->expected_fps);
						} else {
							// Unknown format, estimate based on packet count
							context->expected_fps = (frame_height <= 250) ? 60.0 : 50.0;
							C64U_LOG_WARNING(
								"⚠️ Unknown video format: 384x%u, assuming %.0f Hz",
								frame_height, context->expected_fps);
						}

						// Update context dimensions if they changed
						if (context->height != frame_height) {
							context->height = frame_height;
							context->width = C64U_PIXELS_PER_LINE; // Always 384
						}
					}
				}

				// Check if frame is complete
				if (is_frame_complete(&context->current_frame)) {
					// Assemble complete frame and swap buffers (only if not already completed)
					if (pthread_mutex_lock(&context->frame_mutex) == 0) {
						if (context->last_completed_frame != context->current_frame.frame_num) {
							assemble_frame_to_buffer(context, &context->current_frame);
							swap_frame_buffers(context);
							context->last_completed_frame =
								context->current_frame.frame_num;
							// Track diagnostics (only once per completed frame!)
							context->frames_completed++;
							context->buffer_swaps++;
							context->frames_delivered_to_obs++;
							context->total_pipeline_latency +=
								(os_gettime_ns() - capture_time);
							video_frames++; // Count completed frames for statistics (primary location)
						}
						pthread_mutex_unlock(&context->frame_mutex);
					}

					// Reset for next frame
					init_frame_assembly(&context->current_frame, 0);
				}
			}

			pthread_mutex_unlock(&context->assembly_mutex);
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
	context->auto_detect_ip = obs_data_get_bool(settings, "auto_detect_ip");
	context->video_port = (uint32_t)obs_data_get_int(settings, "video_port");
	context->audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
	context->streaming = false;

	// Initialize OBS IP address from settings or auto-detect on first run
	memset(context->obs_ip_address, 0, sizeof(context->obs_ip_address));
	const char *saved_obs_ip = obs_data_get_string(settings, "obs_ip_address");

	if (saved_obs_ip && strlen(saved_obs_ip) > 0) {
		// Use previously saved/configured OBS IP address
		strncpy(context->obs_ip_address, saved_obs_ip, sizeof(context->obs_ip_address) - 1);
		context->initial_ip_detected = true;
		C64U_LOG_INFO("Using saved OBS IP address: %s", context->obs_ip_address);
	} else {
		// First time - detect local IP address
		if (c64u_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
			C64U_LOG_INFO("Successfully detected OBS IP address: %s", context->obs_ip_address);
			context->initial_ip_detected = true;
			// Save the detected IP to settings for future use
			obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
		} else {
			C64U_LOG_WARNING("Failed to detect OBS IP address, using fallback");
			strncpy(context->obs_ip_address, "192.168.1.100", sizeof(context->obs_ip_address) - 1);
			context->initial_ip_detected = false;
			obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
		}
	}

	// Set default ports if not configured
	if (context->video_port == 0)
		context->video_port = C64U_DEFAULT_VIDEO_PORT;
	if (context->audio_port == 0)
		context->audio_port = C64U_DEFAULT_AUDIO_PORT;

	// Initialize video format (start with PAL, will be detected from stream)
	context->width = C64U_PAL_WIDTH;
	context->height = C64U_PAL_HEIGHT;

	// Allocate video buffers (double buffering)
	size_t frame_size = context->width * context->height * 4; // RGBA
	context->frame_buffer_front = bmalloc(frame_size);
	context->frame_buffer_back = bmalloc(frame_size);
	if (!context->frame_buffer_front || !context->frame_buffer_back) {
		C64U_LOG_ERROR("Failed to allocate video frame buffers");
		if (context->frame_buffer_front)
			bfree(context->frame_buffer_front);
		if (context->frame_buffer_back)
			bfree(context->frame_buffer_back);
		bfree(context);
		return NULL;
	}
	memset(context->frame_buffer_front, 0, frame_size);
	memset(context->frame_buffer_back, 0, frame_size);
	context->frame_ready = false;

	// Initialize video format detection
	context->detected_frame_height = 0;
	context->format_detected = false;
	context->expected_fps = 50.0; // Default to PAL until detected

	// Initialize mutexes
	if (pthread_mutex_init(&context->frame_mutex, NULL) != 0) {
		C64U_LOG_ERROR("Failed to initialize frame mutex");
		bfree(context->frame_buffer_front);
		bfree(context->frame_buffer_back);
		bfree(context);
		return NULL;
	}
	if (pthread_mutex_init(&context->assembly_mutex, NULL) != 0) {
		C64U_LOG_ERROR("Failed to initialize assembly mutex");
		pthread_mutex_destroy(&context->frame_mutex);
		bfree(context->frame_buffer_front);
		bfree(context->frame_buffer_back);
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

	C64U_LOG_INFO("C64U source created - C64 IP: %s, OBS IP: %s, Video: %u, Audio: %u", context->ip_address,
		      context->obs_ip_address, context->video_port, context->audio_port);

	// Auto-start streaming after plugin initialization
	C64U_LOG_INFO("🚀 Auto-starting C64U streaming after plugin initialization...");
	c64u_start_streaming(context);
	context->auto_start_attempted = true;

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
	pthread_mutex_destroy(&context->assembly_mutex);
	if (context->frame_buffer_front) {
		bfree(context->frame_buffer_front);
	}
	if (context->frame_buffer_back) {
		bfree(context->frame_buffer_back);
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
	C64U_LOG_DEBUG("Debug logging %s", c64u_debug_logging ? "enabled" : "disabled"); // Update IP detection setting
	bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
	if (new_auto_detect != context->auto_detect_ip || new_auto_detect) {
		context->auto_detect_ip = new_auto_detect;
		if (new_auto_detect) {
			// Re-detect IP address
			if (c64u_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
				C64U_LOG_INFO("Updated OBS IP address: %s", context->obs_ip_address);
				// Save the updated IP to settings
				obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
			} else {
				C64U_LOG_WARNING("Failed to update OBS IP address");
			}
		}
	}

	// Update configuration
	const char *new_ip = obs_data_get_string(settings, "ip_address");
	const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
	uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
	uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");

	// Set defaults
	if (!new_ip)
		new_ip = C64U_DEFAULT_IP;
	if (new_video_port == 0)
		new_video_port = C64U_DEFAULT_VIDEO_PORT;
	if (new_audio_port == 0)
		new_audio_port = C64U_DEFAULT_AUDIO_PORT;

	// Update configuration
	strncpy(context->ip_address, new_ip, sizeof(context->ip_address) - 1);
	context->ip_address[sizeof(context->ip_address) - 1] = '\0';
	if (new_obs_ip) {
		strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
		context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
	}
	context->video_port = new_video_port;
	context->audio_port = new_audio_port;

	// Always start streaming with current configuration (no stop/restart)
	C64U_LOG_INFO("Applying configuration and starting streaming");
	c64u_start_streaming(context);
}

static void c64u_start_streaming(struct c64u_source *context)
{
	if (!context) {
		C64U_LOG_WARNING("Cannot start streaming - invalid context");
		return;
	}

	// If already streaming, just send start commands again (no need to recreate sockets/threads)
	if (context->streaming) {
		C64U_LOG_INFO("Already streaming - sending start commands with current config");
		send_control_command(context, true, 0); // Start video
		send_control_command(context, true, 1); // Start audio
		return;
	}

	C64U_LOG_INFO("Starting C64U streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
		      context->obs_ip_address, context->video_port, context->audio_port);

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

	// Track render timing for diagnostic purposes
	static uint64_t last_render_time = 0;
	static uint32_t render_calls = 0;
	uint64_t render_start = os_gettime_ns();

	render_calls++;

	// Note: Auto-start streaming now happens in c64u_create, not on first render

	// Check if we have streaming data and frame ready
	if (context->streaming && context->frame_ready && context->frame_buffer_front) {
		// Render actual C64U video frame from front buffer

		// Lock the frame buffer to ensure thread safety
		if (pthread_mutex_lock(&context->frame_mutex) == 0) {
			// Create texture from front buffer data
			gs_texture_t *texture = gs_texture_create(context->width, context->height, GS_RGBA, 1,
								  (const uint8_t **)&context->frame_buffer_front, 0);
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

	// Log render timing diagnostics every 5 seconds
	if (last_render_time > 0) {
		uint64_t render_end = os_gettime_ns();
		uint64_t render_duration = render_end - render_start;
		static uint64_t last_render_log = 0;
		static uint32_t total_render_calls = 0;
		static uint64_t total_render_time = 0;

		total_render_calls++;
		total_render_time += render_duration;

		if (last_render_log == 0)
			last_render_log = render_end;

		uint64_t log_diff = render_end - last_render_log;
		if (log_diff >= 5000000000ULL) { // Every 5 seconds
			double duration = log_diff / 1000000000.0;
			double render_fps = total_render_calls / duration;
			double avg_render_time_ms = total_render_time / (total_render_calls * 1000000.0);

			C64U_LOG_INFO("🎨 RENDER: %.1f fps | %.2f ms avg render time | %u total calls", render_fps,
				      avg_render_time_ms, render_calls);

			// Reset counters
			total_render_calls = 0;
			total_render_time = 0;
			last_render_log = render_end;
		}
	}
	last_render_time = render_start;
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

static obs_properties_t *c64u_properties(void *data)
{
	// C64U properties setup
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	// Debug logging toggle
	obs_property_t *debug_prop = obs_properties_add_bool(props, "debug_logging", "Enable Debug Logging");
	obs_property_set_long_description(debug_prop, "Enable detailed logging for debugging C64U connection issues");

	// C64 IP Address
	obs_property_t *ip_prop = obs_properties_add_text(props, "ip_address", "C64 IP Address", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(
		ip_prop,
		"IP address or DNS name of the C64 Ultimate device. Leave as 0.0.0.0 to skip control commands.");

	// OBS IP Address (editable)
	obs_property_t *obs_ip_prop =
		obs_properties_add_text(props, "obs_ip_address", "OBS IP Address", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(
		obs_ip_prop, "IP address of this OBS machine. C64 Ultimate will stream video/audio to this address.");

	// Auto-detect IP toggle
	obs_property_t *auto_ip_prop = obs_properties_add_bool(props, "auto_detect_ip", "Use Auto-detected OBS IP");
	obs_property_set_long_description(auto_ip_prop,
					  "Use the automatically detected OBS IP address in streaming commands");

	// Video Port
	obs_property_t *video_port_prop = obs_properties_add_int(props, "video_port", "Video Port", 1024, 65535, 1);
	obs_property_set_long_description(video_port_prop, "UDP port for video stream (default: 11000)");

	// Audio Port
	obs_property_t *audio_port_prop = obs_properties_add_int(props, "audio_port", "Audio Port", 1024, 65535, 1);
	obs_property_set_long_description(audio_port_prop, "UDP port for audio stream (default: 11001)");

	return props;
}

static void c64u_defaults(obs_data_t *settings)
{
	// C64U defaults initialization

	obs_data_set_default_bool(settings, "debug_logging", true);
	obs_data_set_default_bool(settings, "auto_detect_ip", true);
	obs_data_set_default_string(settings, "ip_address", C64U_DEFAULT_IP);
	obs_data_set_default_string(settings, "obs_ip_address", ""); // Empty by default, will be auto-detected
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
