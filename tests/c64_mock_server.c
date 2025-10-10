/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#define _GNU_SOURCE // Enable usleep and other GNU extensions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <errno.h>

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close(s) closesocket(s)
typedef int socklen_t;
typedef long ssize_t;
typedef HANDLE pthread_t;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#endif

// C64U constants matching the plugin
#define C64_VIDEO_PACKET_SIZE 780
#define C64_AUDIO_PACKET_SIZE 770
#define C64_VIDEO_HEADER_SIZE 12
#define C64_AUDIO_HEADER_SIZE 2
#define C64_CONTROL_PORT 64
#define C64_VIDEO_PORT 11000
#define C64_AUDIO_PORT 11001

// Video format constants
#define C64_PAL_WIDTH 384
#define C64_PAL_HEIGHT 272
#define C64_PIXELS_PER_LINE 384
#define C64_LINES_PER_PACKET 4

// Mock server state
struct mock_server {
    int control_socket;
    int video_socket;
    int audio_socket;
    pthread_t control_thread;
    pthread_t video_thread;
    pthread_t audio_thread;
    volatile int running;
    volatile int video_streaming;
    volatile int audio_streaming;
    char client_ip[64];
};

static struct mock_server server = {0};

// Generate test pattern for video
static void generate_test_pattern(uint8_t *pixel_data, int frame_num, int line_num)
{
    // Simple test pattern: alternating colors based on position and frame
    for (int line = 0; line < C64_LINES_PER_PACKET; line++) {
        for (int x = 0; x < C64_PIXELS_PER_LINE / 2; x++) {
            int pixel_line = line_num + line;
            uint8_t color1 = ((x + pixel_line + frame_num) % 16);
            uint8_t color2 = ((x + pixel_line + frame_num + 1) % 16);

            int offset = line * (C64_PIXELS_PER_LINE / 2) + x;
            pixel_data[offset] = (color2 << 4) | color1;
        }
    }
}

// Video streaming thread
static void *video_thread_func(void *data)
{
    (void)data; // Suppress unused parameter warning
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        uint8_t packet[C64_VIDEO_PACKET_SIZE];
        uint16_t frame_num = 0;

        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(C64_VIDEO_PORT);
        inet_pton(AF_INET, server.client_ip, &client_addr.sin_addr);

        printf("Video thread started, sending to %s:%d\n", server.client_ip, C64_VIDEO_PORT);

        while (server.running) {
            if (!server.video_streaming) {
                usleep(10000); // 10ms
                continue;
            }

            // Send one frame (PAL format: 68 packets of 4 lines each)
            for (int packet_num = 0; packet_num < 68; packet_num++) {
                uint16_t line_num = packet_num * C64_LINES_PER_PACKET;
                bool last_packet = (packet_num == 67);

                // Build packet header
                *(uint16_t *)(packet + 0) = packet_num;                            // sequence number
                *(uint16_t *)(packet + 2) = frame_num;                             // frame number
                *(uint16_t *)(packet + 4) = line_num | (last_packet ? 0x8000 : 0); // line number + flag
                *(uint16_t *)(packet + 6) = C64_PIXELS_PER_LINE;                   // pixels per line
                packet[8] = C64_LINES_PER_PACKET;                                  // lines per packet
                packet[9] = 4;                                                     // bits per pixel
                *(uint16_t *)(packet + 10) = 0;                                    // encoding type

                // Generate test pattern
                generate_test_pattern(packet + C64_VIDEO_HEADER_SIZE, frame_num, line_num);

                // Send packet
                ssize_t sent = sendto(server.video_socket, packet, C64_VIDEO_PACKET_SIZE, 0,
                                      (struct sockaddr *)&client_addr, client_len);
                if (sent < 0) {
                    printf("Video send error: %s\n", strerror(errno));
                }

                usleep(1000); // 1ms between packets
            }

            frame_num++;
            usleep(20000); // ~50fps (20ms per frame)
        }

        printf("Video thread stopped\n");
        return NULL;
    }
}

// Audio streaming thread
static void *audio_thread_func(void *data)
{
    (void)data; // Suppress unused parameter warning
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t packet[C64_AUDIO_PACKET_SIZE];
    uint16_t seq_num = 0;

    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(C64_AUDIO_PORT);
    inet_pton(AF_INET, server.client_ip, &client_addr.sin_addr);

    printf("Audio thread started, sending to %s:%d\n", server.client_ip, C64_AUDIO_PORT);

    while (server.running) {
        if (!server.audio_streaming) {
            usleep(10000); // 10ms
            continue;
        }

        // Build packet header
        *(uint16_t *)(packet) = seq_num++;

        // Generate test audio (simple sine wave)
        int16_t *audio_data = (int16_t *)(packet + C64_AUDIO_HEADER_SIZE);
        for (int i = 0; i < 192; i++) { // 192 stereo samples
            float t = (seq_num * 192 + i) / 48000.0f;
            int16_t sample = (int16_t)(sin(t * 2 * 3.14159 * 440) * 8000); // 440Hz tone
            audio_data[i * 2] = sample;                                    // Left channel
            audio_data[i * 2 + 1] = sample;                                // Right channel
        }

        // Send packet
        ssize_t sent =
            sendto(server.audio_socket, packet, C64_AUDIO_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, client_len);
        if (sent < 0) {
            printf("Audio send error: %s\n", strerror(errno));
        }

        usleep(4000); // ~250Hz packet rate (4ms per packet for 192 samples at 48kHz)
    }

    printf("Audio thread stopped\n");
    return NULL;
}

// Control server thread
static void *control_thread_func(void *data)
{
    (void)data; // Suppress unused parameter warning
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Setup server socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(C64_CONTROL_PORT);

    if (bind(server.control_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Control bind failed: %s\n", strerror(errno));
        return NULL;
    }

    if (listen(server.control_socket, 5) < 0) {
        printf("Control listen failed: %s\n", strerror(errno));
        return NULL;
    }

    printf("Control server listening on port %d\n", C64_CONTROL_PORT);

    while (server.running) {
        int client_sock = accept(server.control_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (server.running) {
                printf("Control accept failed: %s\n", strerror(errno));
            }
            continue;
        }

        // Store client IP for streaming
        inet_ntop(AF_INET, &client_addr.sin_addr, server.client_ip, sizeof(server.client_ip));
        printf("Control connection from %s\n", server.client_ip);

        uint8_t cmd[6];
        ssize_t received = recv(client_sock, cmd, sizeof(cmd), 0);

        if (received >= 4) {
            printf("Received command: ");
            for (int i = 0; i < received; i++) {
                printf("%02X ", cmd[i]);
            }
            printf("\n");

            // Parse command
            if (cmd[0] == 0x20 && cmd[1] == 0xFF) {
                // Start stream command
                uint8_t stream_id = cmd[2] - 0x02;
                if (stream_id == 0) {
                    server.video_streaming = 1;
                    printf("Video streaming started\n");
                } else if (stream_id == 1) {
                    server.audio_streaming = 1;
                    printf("Audio streaming started\n");
                }
            } else if (cmd[0] == 0x30 && cmd[1] == 0xFF) {
                // Stop stream command
                uint8_t stream_id = cmd[2] - 0x03;
                if (stream_id == 0) {
                    server.video_streaming = 0;
                    printf("Video streaming stopped\n");
                } else if (stream_id == 1) {
                    server.audio_streaming = 0;
                    printf("Audio streaming stopped\n");
                }
            }
        }

        close(client_sock);
    }

    printf("Control thread stopped\n");
    return NULL;
}

static void signal_handler(int sig)
{
    (void)sig; // Suppress unused parameter warning
    printf("\nShutting down mock server...\n");
    server.running = 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv; // Suppress unused parameter warnings

    printf("C64 Mock Server v1.0\n");
    printf("Simulating C64 Ultimate device for testing\n\n");

    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize server
    server.running = 1;
    server.video_streaming = 0;
    server.audio_streaming = 0;
    strcpy(server.client_ip, "127.0.0.1");

    // Create sockets
    server.control_socket = socket(AF_INET, SOCK_STREAM, 0);
    server.video_socket = socket(AF_INET, SOCK_DGRAM, 0);
    server.audio_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (server.control_socket < 0 || server.video_socket < 0 || server.audio_socket < 0) {
        printf("Failed to create sockets: %s\n", strerror(errno));
        return 1;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(server.control_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Start threads
    if (pthread_create(&server.control_thread, NULL, control_thread_func, NULL) != 0) {
        printf("Failed to create control thread\n");
        return 1;
    }

    if (pthread_create(&server.video_thread, NULL, video_thread_func, NULL) != 0) {
        printf("Failed to create video thread\n");
        return 1;
    }

    if (pthread_create(&server.audio_thread, NULL, audio_thread_func, NULL) != 0) {
        printf("Failed to create audio thread\n");
        return 1;
    }

    printf("Mock server started. Press Ctrl+C to stop.\n");
    printf("Configure OBS plugin to connect to: 127.0.0.1\n\n");

    // Wait for threads to finish
    pthread_join(server.control_thread, NULL);
    pthread_join(server.video_thread, NULL);
    pthread_join(server.audio_thread, NULL);

    // Cleanup
    close(server.control_socket);
    close(server.video_socket);
    close(server.audio_socket);

    printf("Mock server stopped.\n");
    return 0;
}
