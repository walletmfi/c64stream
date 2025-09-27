/*
C64U Plugin Integration Tests
Copyright (C) 2025 Chris Gleissner

Integration tests that verify the plugin works end-to-end with the mock server.
This simulates OBS Studio calling the plugin functions and verifies correct
video and audio stream processing.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

// Mock OBS structures and functions (minimal implementation for testing)
typedef struct obs_source obs_source_t;
typedef struct obs_data obs_data_t;
typedef struct obs_properties obs_properties_t;

// Mock OBS API functions that the plugin expects
obs_data_t *obs_data_create(void) {
    return calloc(1, 64); // Simple mock
}

void obs_data_release(obs_data_t *data) {
    free(data);
}

const char *obs_data_get_string(obs_data_t *data, const char *name) {
    (void)data; (void)name;
    if (strcmp(name, "ip_address") == 0) return "127.0.0.1";
    if (strcmp(name, "video_port") == 0) return "11000"; 
    if (strcmp(name, "audio_port") == 0) return "11001";
    return "";
}

int obs_data_get_int(obs_data_t *data, const char *name) {
    (void)data;
    if (strcmp(name, "video_port") == 0) return 11000;
    if (strcmp(name, "audio_port") == 0) return 11001;
    return 0;
}

bool obs_data_get_bool(obs_data_t *data, const char *name) {
    (void)data; (void)name;
    return false; // Default values
}

void obs_data_set_string(obs_data_t *data, const char *name, const char *val) {
    (void)data; (void)name; (void)val;
}

void obs_data_set_int(obs_data_t *data, const char *name, int val) {
    (void)data; (void)name; (void)val;
}

void obs_data_set_bool(obs_data_t *data, const char *name, bool val) {
    (void)data; (void)name; (void)val;
}

obs_properties_t *obs_properties_create(void) {
    return calloc(1, 64);
}

// Mock graphics and audio functions
void gs_technique_begin(void *tech) { (void)tech; }
void gs_technique_end(void *tech) { (void)tech; }
void gs_technique_begin_pass(void *tech, int pass) { (void)tech; (void)pass; }
void gs_technique_end_pass(void *tech) { (void)tech; }
void gs_draw_sprite(void *tex, int flags, int width, int height) { 
    (void)tex; (void)flags; (void)width; (void)height; 
}

// Mock effect functions
void *obs_get_base_effect(int type) { (void)type; return (void*)0x1234; }
void *gs_effect_get_technique(void *effect, const char *name) { (void)effect; (void)name; return (void*)0x1234; }
void *gs_effect_get_param_by_name(void *effect, const char *name) { (void)effect; (void)name; return (void*)0x1234; }
void gs_effect_set_vec4(void *param, void *vec) { (void)param; (void)vec; }

// Mock texture functions  
void *gs_texture_create(int width, int height, int format, int levels, void *data, int flags) {
    (void)width; (void)height; (void)format; (void)levels; (void)data; (void)flags;
    return (void*)0x5678;
}

void gs_texture_destroy(void *tex) { (void)tex; }
void gs_texture_set_image(void *tex, void *data, int linesize, bool invert) {
    (void)tex; (void)data; (void)linesize; (void)invert;
}

// Mock audio functions
void obs_source_output_audio(obs_source_t *source, void *frames) {
    (void)source; (void)frames;
    // In a real test, we'd verify the audio data here
    printf("    Audio frame received ✓\n");
}

// Mock logging functions
#define C64U_LOG_INFO(fmt, ...) printf("[C64U INFO] " fmt "\n", ##__VA_ARGS__)
#define C64U_LOG_WARNING(fmt, ...) printf("[C64U WARN] " fmt "\n", ##__VA_ARGS__)
#define C64U_LOG_ERROR(fmt, ...) printf("[C64U ERROR] " fmt "\n", ##__VA_ARGS__)
#define C64U_LOG_DEBUG(fmt, ...) printf("[C64U DEBUG] " fmt "\n", ##__VA_ARGS__)

// Include the plugin source directly (so we can test internal functions)
// We need to define some constants that would normally come from OBS headers
#define OBS_SOURCE_TYPE_INPUT 0
#define OBS_SOURCE_VIDEO 1
#define OBS_SOURCE_AUDIO 2
#define OBS_EFFECT_SOLID 0
#define GS_RGBA 0
#define GS_DYNAMIC 0

// Mock some additional OBS functions that might be called
void *bzalloc(size_t size) { return calloc(1, size); }
void bfree(void *ptr) { free(ptr); }
void obs_register_source(void *info) { (void)info; }

// Global test state
static bool g_frame_received = false;
static bool g_audio_received = false;
static int g_frames_count = 0;
static int g_audio_count = 0;

// Override some plugin functions to track what happens
#define gs_texture_create test_gs_texture_create
#define obs_source_output_audio test_obs_source_output_audio

void *test_gs_texture_create(int width, int height, int format, int levels, void *data, int flags) {
    (void)width; (void)height; (void)format; (void)levels; (void)flags;
    if (data) {
        g_frame_received = true;
        g_frames_count++;
        printf("    Video frame %d received (%dx%d) ✓\n", g_frames_count, width, height);
    }
    return (void*)0x5678;
}

void test_obs_source_output_audio(obs_source_t *source, void *frames) {
    (void)source; (void)frames;
    g_audio_received = true;
    g_audio_count++;
    printf("    Audio frame %d received ✓\n", g_audio_count);
}

// Now include the plugin source
#include "../src/plugin-main.c"

// Test helper functions
static pid_t start_mock_server(void) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - run mock server
        execl("./c64u_mock_server", "c64u_mock_server", NULL);
        perror("Failed to start mock server");
        exit(1);
    } else if (pid > 0) {
        // Parent process - wait a bit for server to start
        printf("Started mock server (PID: %d)\n", pid);
        sleep(2); // Give server time to start
        return pid;
    } else {
        perror("Failed to fork mock server");
        return -1;
    }
}

static void stop_mock_server(pid_t pid) {
    if (pid > 0) {
        printf("Stopping mock server (PID: %d)\n", pid);
        kill(pid, SIGTERM);
        
        // Wait for it to stop
        int status;
        waitpid(pid, &status, 0);
        printf("Mock server stopped\n");
    }
}

static bool wait_for_data(int timeout_sec) {
    printf("  Waiting up to %d seconds for data...\n", timeout_sec);
    
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (g_frame_received && g_audio_received) {
            printf("  Both video and audio data received!\n");
            return true;
        }
        usleep(100000); // 100ms
        
        // Show progress
        if (i % 10 == 0) {
            printf("    %ds: frames=%d, audio=%d\n", i/10, g_frames_count, g_audio_count);
        }
    }
    
    printf("  Timeout waiting for data (frames=%d, audio=%d)\n", g_frames_count, g_audio_count);
    return false;
}

// Main integration test
void test_plugin_integration(void) {
    printf("\n=== C64U Plugin Integration Test ===\n");
    
    // Step 1: Start mock server
    printf("\n1. Starting mock server...\n");
    pid_t server_pid = start_mock_server();
    assert(server_pid > 0);
    
    // Step 2: Create plugin instance (simulate OBS creating source)
    printf("\n2. Creating plugin instance...\n");
    obs_data_t *settings = obs_data_create();
    obs_source_t *mock_source = (obs_source_t*)0x9999; // Mock source pointer
    
    // Set up test configuration
    obs_data_set_string(settings, "ip_address", "127.0.0.1");
    obs_data_set_int(settings, "video_port", 11000);
    obs_data_set_int(settings, "audio_port", 11001);
    obs_data_set_bool(settings, "debug_logging", true);
    
    struct c64u_source *context = (struct c64u_source*)c64u_create(settings, mock_source);
    assert(context != NULL);
    printf("  Plugin instance created ✓\n");
    
    // Step 3: Start streaming (simulate user clicking "Start Streaming")
    printf("\n3. Starting streaming...\n");
    g_frame_received = false;
    g_audio_received = false;
    g_frames_count = 0;
    g_audio_count = 0;
    
    c64u_start_streaming(context);
    printf("  Streaming started ✓\n");
    
    // Step 4: Wait for data and simulate OBS render calls
    printf("\n4. Testing data reception...\n");
    bool data_received = false;
    
    for (int attempt = 0; attempt < 3; attempt++) {
        printf("  Attempt %d:\n", attempt + 1);
        
        // Simulate OBS calling render function multiple times
        for (int i = 0; i < 10; i++) {
            c64u_render(context, NULL); // Simulate OBS render call
            usleep(50000); // 50ms between renders (20 FPS)
        }
        
        if (wait_for_data(5)) {
            data_received = true;
            break;
        }
        
        printf("  Retrying...\n");
    }
    
    // Step 5: Verify results
    printf("\n5. Verifying results...\n");
    if (data_received) {
        printf("  ✓ Integration test PASSED!\n");
        printf("    - Video frames received: %d\n", g_frames_count);
        printf("    - Audio frames received: %d\n", g_audio_count);
    } else {
        printf("  ✗ Integration test FAILED - insufficient data received\n");
        printf("    - Video frames received: %d\n", g_frames_count);
        printf("    - Audio frames received: %d\n", g_audio_count);
    }
    
    // Step 6: Cleanup
    printf("\n6. Cleaning up...\n");
    c64u_stop_streaming(context);
    c64u_destroy(context);
    obs_data_release(settings);
    stop_mock_server(server_pid);
    
    printf("  Cleanup completed ✓\n");
    
    // Assert final results
    assert(data_received);
    assert(g_frames_count > 0);
    assert(g_audio_count > 0);
    
    printf("\n=== Integration Test PASSED ===\n");
}

int main(void) {
    printf("C64U Plugin Integration Tests\n");
    printf("============================\n");
    
    // Change to tests directory so we can find the mock server
    if (chdir("tests") != 0) {
        // Try current directory
        printf("Running from current directory\n");
    }
    
    // Check if mock server exists
    if (access("./c64u_mock_server", X_OK) != 0) {
        printf("Error: c64u_mock_server not found or not executable\n");
        printf("Please build the tests first:\n");
        printf("  cmake --build build_x86_64 --target c64u_mock_server\n");
        return 1;
    }
    
    test_plugin_integration();
    
    printf("\nAll integration tests PASSED! ✓\n");
    return 0;
}