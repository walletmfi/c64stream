#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include "c64-network.h"
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-source.h"

// Forward declarations

#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-color.h"
#include "c64-audio.h"
#include "c64-logo.h"
#include "c64-record.h"
#include "c64-version.h"
#include "c64-properties.h"
#include "plugin-support.h"

// Forward declarations
static void close_and_reset_sockets(struct c64_source *context);

// Async retry task - runs in OBS thread pool (NOT render thread)
// Based on working 0.4.3 approach but simplified
static void c64_async_retry_task(void *data)
{
    struct c64_source *context = (struct c64_source *)data;

    if (!context) {
        C64_LOG_WARNING("Async retry task called with NULL context");
        return;
    }

    C64_LOG_INFO("Async retry attempt %u - %s", context->retry_count,
                 context->streaming ? "sending start commands" : "starting streaming");

    bool tcp_success = false;

    if (!context->streaming) {

        c64_start_streaming(context);
        tcp_success = true; // c64_start_streaming handles TCP commands internally
    } else {
        // Already streaming - test connectivity and send start commands (like 0.4.3)
        // Use quick connectivity test (250ms timeout) instead of blocking TCP socket creation
        if (c64_test_connectivity(context->ip_address, C64_CONTROL_PORT)) {
            c64_send_control_command(context, true, 0); // Video
            c64_send_control_command(context, true, 1); // Audio
            tcp_success = true;
            context->consecutive_failures = 0; // Reset failure counter on success
        } else {
            tcp_success = false;
            context->consecutive_failures++;
        }
    }

    context->retry_count++;

    if (!tcp_success) {
        C64_LOG_DEBUG("TCP connection failed (%u consecutive failures)", context->consecutive_failures);
    }

    // Clear retry state - allows future retries
    context->retry_in_progress = false;
}

// Helper function to safely close and reset sockets
static void close_and_reset_sockets(struct c64_source *context)
{
    if (context->video_socket != INVALID_SOCKET_VALUE) {
        close(context->video_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
    }
    if (context->audio_socket != INVALID_SOCKET_VALUE) {
        close(context->audio_socket);
        context->audio_socket = INVALID_SOCKET_VALUE;
    }
}

void *c64_create(obs_data_t *settings, obs_source_t *source)
{
    C64_LOG_INFO("Creating C64S source");

    // C64S source creation

    // Initialize networking on first use
    static bool networking_initialized = false;
    if (!networking_initialized) {
        if (!c64_init_networking()) {
            C64_LOG_ERROR("Failed to initialize networking");
            return NULL;
        }
        networking_initialized = true;
    }

    // Initialize color conversion optimization on first use
    static bool color_lut_initialized = false;
    if (!color_lut_initialized) {
        c64_init_color_conversion_lut();
        color_lut_initialized = true;
    }

    struct c64_source *context = bzalloc(sizeof(struct c64_source));
    if (!context) {
        C64_LOG_ERROR("Failed to allocate memory for source context");
        return NULL;
    }

    context->source = source;

    // Initialize configuration from settings
    const char *host = obs_data_get_string(settings, "c64_host");
    const char *hostname = host ? host : C64_DEFAULT_HOST;

    // Store the original hostname/IP as entered by user
    strncpy(context->hostname, hostname, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for actual connections
    if (!c64_resolve_hostname_with_dns(hostname, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, hostname, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64_LOG_WARNING("Could not resolve hostname '%s', using as-is: %s", hostname, context->ip_address);
    } else {
        C64_LOG_INFO("Resolved C64 Ultimate host '%s' to IP: %s", hostname, context->ip_address);
    }

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
        C64_LOG_INFO("Using saved OBS IP address: %s", context->obs_ip_address);
    } else {
        // First time - detect local IP address
        if (c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
            C64_LOG_INFO("Successfully detected OBS IP address: %s", context->obs_ip_address);
            context->initial_ip_detected = true;
            // Save the detected IP to settings for future use
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        } else {
            C64_LOG_WARNING("Failed to detect OBS IP address, will use configured value");
            context->initial_ip_detected = false;
        }
    }

    // Ensure we have a valid OBS IP address - use localhost as last resort
    if (strlen(context->obs_ip_address) == 0) {
        C64_LOG_INFO("No OBS IP configured, using localhost as fallback");
        strncpy(context->obs_ip_address, "127.0.0.1", sizeof(context->obs_ip_address) - 1);
        obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
    }

    // Set default ports if not configured
    if (context->video_port == 0)
        context->video_port = C64_DEFAULT_VIDEO_PORT;
    if (context->audio_port == 0)
        context->audio_port = C64_DEFAULT_AUDIO_PORT;

    // Initialize video format (start with PAL, will be detected from stream)
    context->width = C64_PAL_WIDTH;
    context->height = C64_PAL_HEIGHT;

    // Allocate single frame buffer for direct async video output (RGBA, 4 bytes per pixel)
    size_t frame_size = context->width * context->height * sizeof(uint32_t);
    context->frame_buffer = bmalloc(frame_size);
    if (!context->frame_buffer) {
        C64_LOG_ERROR("Failed to allocate frame buffer");
        return NULL;
    }
    memset(context->frame_buffer, 0, frame_size);

    // Allocate pre-allocated recording buffers to eliminate malloc/free in hot paths
    context->recording_buffer_size = frame_size;                               // Same as RGBA frame size
    context->bmp_row_buffer = bmalloc(context->width * 4 + 4);                 // Row + padding for BMP
    context->bgr_frame_buffer = bmalloc(context->width * context->height * 3); // BGR24 frame
    if (!context->bmp_row_buffer || !context->bgr_frame_buffer) {
        C64_LOG_ERROR("Failed to allocate recording buffers");
        if (context->frame_buffer)
            bfree(context->frame_buffer);
        if (context->bmp_row_buffer)
            bfree(context->bmp_row_buffer);
        if (context->bgr_frame_buffer)
            bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }
    context->last_frame_time = 0; // Initialize frame timeout detection

    // Initialize video format detection
    context->detected_frame_height = 0;
    context->format_detected = false;
    context->expected_fps = 50.125; // Default to PAL timing until detected

    // Initialize mutexes (frame_mutex no longer needed for async video output)
    if (pthread_mutex_init(&context->assembly_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize assembly mutex");
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
        bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Initialize buffer delay from settings
    context->buffer_delay_ms = (uint32_t)obs_data_get_int(settings, "buffer_delay_ms");
    if (context->buffer_delay_ms == 0) {
        context->buffer_delay_ms = 10; // Default 10ms
    }

    // Initialize network buffer for packet jitter correction
    context->network_buffer = c64_network_buffer_create();
    if (!context->network_buffer) {
        C64_LOG_ERROR("Failed to create network buffer");
        if (context->frame_buffer)
            bfree(context->frame_buffer);
        if (context->bmp_row_buffer)
            bfree(context->bmp_row_buffer);
        if (context->bgr_frame_buffer)
            bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Set initial buffer delay for both video and audio
    c64_network_buffer_set_delay(context->network_buffer, context->buffer_delay_ms, context->buffer_delay_ms);

    C64_LOG_INFO("Network buffer initialized: %u ms delay", context->buffer_delay_ms);

    // Initialize sockets to invalid
    context->video_socket = INVALID_SOCKET_VALUE;
    context->audio_socket = INVALID_SOCKET_VALUE;
    context->control_socket = INVALID_SOCKET_VALUE;
    context->thread_active = false;
    context->video_thread_active = false;
    context->video_processor_thread_active = false;
    context->audio_thread_active = false;
    context->auto_start_attempted = false;

    // Initialize statistics counters
    context->video_packets_received = 0;
    context->video_bytes_received = 0;
    context->video_sequence_errors = 0;
    context->video_frames_processed = 0;
    context->audio_packets_received = 0;
    context->audio_bytes_received = 0;
    context->last_stats_log_time = os_gettime_ns();

    // Initialize render callback timeout system
    uint64_t now = os_gettime_ns();
    context->last_udp_packet_time = now;
    context->retry_in_progress = false;
    context->retry_count = 0;
    context->consecutive_failures = 0;

    // Initialize ideal timestamp generation
    context->stream_start_time_ns = 0;
    context->first_frame_num = 0;
    context->timestamp_base_set = false;
    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS; // Default to PAL, will be updated on detection

    // Initialize logo system with pre-rendered frame
    if (!c64_logo_init(context)) {
        C64_LOG_WARNING("Logo system initialization failed - continuing without logo");
    }

    // Initialize recording for this source
    c64_record_init(context); // Start initial connection asynchronously to avoid blocking OBS startup
    C64_LOG_INFO("C64S source created successfully - queuing async initial connection");
    context->retry_in_progress = true; // Prevent render thread from also starting retry
    obs_queue_task(OBS_TASK_UI, c64_async_retry_task, context, false);

    return context;
}

void c64_destroy(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return;

    C64_LOG_INFO("Destroying C64S source");

    // No retry thread to shutdown - using async delegation approach

    // Stop streaming if active
    if (context->streaming) {
        C64_LOG_DEBUG("Stopping active streaming during destruction");
        context->streaming = false;
        context->thread_active = false;

        close_and_reset_sockets(context);
        if (context->video_thread_active) {
            pthread_join(context->video_thread, NULL);
            context->video_thread_active = false;
        }
        if (context->video_processor_thread_active) {
            pthread_join(context->video_processor_thread, NULL);
            context->video_processor_thread_active = false;
        }
        if (context->audio_thread_active) {
            pthread_join(context->audio_thread, NULL);
            context->audio_thread_active = false;
        }
    }

    c64_record_cleanup(context);

    // Cleanup logo system
    c64_logo_cleanup(context);

    // Cleanup resources
    pthread_mutex_destroy(&context->assembly_mutex);
    if (context->frame_buffer) {
        bfree(context->frame_buffer);
    }
    if (context->bmp_row_buffer) {
        bfree(context->bmp_row_buffer);
    }
    if (context->bgr_frame_buffer) {
        bfree(context->bgr_frame_buffer);
    }
    if (context->network_buffer) {
        c64_network_buffer_destroy(context->network_buffer);
        context->network_buffer = NULL;
    }

    bfree(context);
    C64_LOG_INFO("C64S source destroyed");
}

void c64_update(void *data, obs_data_t *settings)
{
    struct c64_source *context = data;
    if (!context)
        return;

    // Update debug logging setting
    c64_debug_logging = obs_data_get_bool(settings, "debug_logging");
    C64_LOG_DEBUG("Debug logging %s", c64_debug_logging ? "enabled" : "disabled"); // Update IP detection setting
    bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
    if (new_auto_detect != context->auto_detect_ip || new_auto_detect) {
        context->auto_detect_ip = new_auto_detect;
        if (new_auto_detect) {
            // Re-detect IP address
            if (c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
                C64_LOG_INFO("Updated OBS IP address: %s", context->obs_ip_address);
                // Save the updated IP to settings
                obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
            } else {
                C64_LOG_WARNING("Failed to update OBS IP address");
            }
        }
    }

    // Update configuration
    const char *new_host = obs_data_get_string(settings, "c64_host");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");

    // Set defaults
    if (!new_host)
        new_host = C64_DEFAULT_HOST;
    if (new_video_port == 0)
        new_video_port = C64_DEFAULT_VIDEO_PORT;
    if (new_audio_port == 0)
        new_audio_port = C64_DEFAULT_AUDIO_PORT;

    // Check if ports have changed (requires socket recreation)
    bool ports_changed = (new_video_port != context->video_port) || (new_audio_port != context->audio_port);

    if (ports_changed && context->streaming) {
        C64_LOG_INFO("Port configuration changed (video: %u->%u, audio: %u->%u), recreating sockets",
                     context->video_port, new_video_port, context->audio_port, new_audio_port);

        // Stop streaming and close existing sockets
        c64_stop_streaming(context);

        // Give the C64 Ultimate device time to process stop commands
        os_sleep_ms(100);
    }

    // Update configuration - hostname and IP resolution
    strncpy(context->hostname, new_host, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for connections
    if (!c64_resolve_hostname_with_dns(new_host, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, new_host, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64_LOG_WARNING("Could not resolve hostname '%s' during update, using as-is: %s", new_host,
                        context->ip_address);
    } else {
        C64_LOG_DEBUG("Resolved C64 Ultimate host '%s' to IP: %s", new_host, context->ip_address);
    }
    if (new_obs_ip) {
        strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
    }
    context->video_port = new_video_port;
    context->audio_port = new_audio_port;

    // Update buffer delay setting with debouncing to prevent timestamp reset storms
    uint32_t new_buffer_delay_ms = (uint32_t)obs_data_get_int(settings, "buffer_delay_ms");
    if (new_buffer_delay_ms != context->buffer_delay_ms) {
        uint32_t old_buffer_delay_ms = context->buffer_delay_ms;
        C64_LOG_INFO("Buffer delay changed from %u to %u ms", old_buffer_delay_ms, new_buffer_delay_ms);

        context->buffer_delay_ms = new_buffer_delay_ms;

        // Update network buffer delay (this adjusts UDP packet buffering only)
        if (context->network_buffer) {
            c64_network_buffer_set_delay(context->network_buffer, new_buffer_delay_ms, new_buffer_delay_ms);
        }
    }

    // Update recording settings
    c64_record_update_settings(context, settings);

    // Start streaming with current configuration (will create new sockets if needed)
    C64_LOG_INFO("Applying configuration and starting streaming");
    c64_start_streaming(context);
}

void c64_start_streaming(struct c64_source *context)
{
    if (!context) {
        C64_LOG_WARNING("Cannot start streaming - invalid context");
        return;
    }

    C64_LOG_INFO("Starting C64S streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
                 context->obs_ip_address, context->video_port, context->audio_port);

    // Stop existing threads BEFORE closing sockets (prevents race conditions on Windows)
    if (context->streaming) {
        context->streaming = false;
        context->thread_active = false;

        // Wait for existing threads to finish BEFORE closing their sockets
        if (context->video_thread_active && pthread_join(context->video_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing video thread during reconnection");
        }
        if (context->audio_thread_active && pthread_join(context->audio_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing audio thread during reconnection");
        }
        context->video_thread_active = false;
        context->audio_thread_active = false;
    }

    // Now safe to close existing sockets after threads have stopped
    close_and_reset_sockets(context);

    // Create fresh UDP sockets (critical for reconnection after C64S restart)
    context->video_socket = c64_create_udp_socket(context->video_port);
    context->audio_socket = c64_create_udp_socket(context->audio_port);

    if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
        C64_LOG_ERROR("Failed to create UDP sockets for streaming");
        close_and_reset_sockets(context);
        return;
    }

    // Send start commands to C64 Ultimate
    c64_send_control_command(context, true, 0); // Start video
    c64_send_control_command(context, true, 1); // Start audio

    // Start fresh worker threads
    context->thread_active = true;
    context->streaming = true;

    if (pthread_create(&context->video_thread, NULL, c64_video_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create video receiver thread");
        context->streaming = false;
        context->thread_active = false;
        close_and_reset_sockets(context);
        return;
    }
    context->video_thread_active = true;

    // Start video processor thread (processes packets from network buffer)
    if (pthread_create(&context->video_processor_thread, NULL, c64_video_processor_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create video processor thread");
        context->streaming = false;
        context->thread_active = false;
        if (context->video_thread_active) {
            pthread_join(context->video_thread, NULL);
            context->video_thread_active = false;
        }
        close_and_reset_sockets(context);
        return;
    }
    context->video_processor_thread_active = true;

    if (pthread_create(&context->audio_thread, NULL, audio_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create audio receiver thread");
        context->streaming = false;
        context->thread_active = false;
        if (context->video_thread_active) {
            pthread_join(context->video_thread, NULL);
            context->video_thread_active = false;
        }
        if (context->video_processor_thread_active) {
            pthread_join(context->video_processor_thread, NULL);
            context->video_processor_thread_active = false;
        }
        close_and_reset_sockets(context);
        return;
    }
    context->audio_thread_active = true;

    C64_LOG_INFO("C64S streaming started successfully");
}

void c64_stop_streaming(struct c64_source *context)
{
    if (!context || !context->streaming) {
        C64_LOG_WARNING("Cannot stop streaming - invalid context or not streaming");
        return;
    }

    C64_LOG_INFO("Stopping C64S streaming...");

    context->streaming = false;
    context->thread_active = false;

    close_and_reset_sockets(context);
    if (context->video_thread_active && pthread_join(context->video_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join video thread");
    }
    context->video_thread_active = false;

    if (context->video_processor_thread_active && pthread_join(context->video_processor_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join video processor thread");
    }
    context->video_processor_thread_active = false;

    if (context->audio_thread_active && pthread_join(context->audio_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join audio thread");
    }
    context->audio_thread_active = false;

    // Clear frame buffer (async video will stop automatically)
    if (context->frame_buffer) {
        uint32_t frame_size = context->width * context->height * 4;
        memset(context->frame_buffer, 0, frame_size);
    }

    // Reset frame assembly state
    if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
        memset(&context->current_frame, 0, sizeof(context->current_frame));
        context->last_completed_frame = 0;
        context->frame_drops = 0;
        context->packet_drops = 0;
        context->frames_expected = 0;
        context->frames_captured = 0;
        context->frames_delivered_to_obs = 0;
        context->frames_completed = 0;
        pthread_mutex_unlock(&context->assembly_mutex);
    }

    C64_LOG_INFO("C64S streaming stopped");
}

// Synchronous render callback removed - now using async video output via obs_source_output_video()

const char *c64_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("C64Stream");
}

obs_properties_t *c64_properties(void *data)
{
    return c64_create_properties(data);
}

void c64_defaults(obs_data_t *settings)
{
    c64_set_property_defaults(settings);
}
