/**
 * Recording system coordinator for C64 Ultimate streaming
 * Manages recording sessions, delegates to specialized modules for different formats
 */

#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "c64-logging.h"
#include "c64-record.h"
#include "c64-record-obs.h"
#include "c64-record-network.h"
#include "c64-record-video.h"
#include "c64-record-audio.h"
#include "c64-record-frames.h"
#include "c64-types.h"

#ifndef S_ISDIR
#ifdef _WIN32
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#endif

// Shared utility functions

/**
 * Create directory path recursively (cross-platform)
 * @param path Directory path to create
 * @return true if successful or directory exists, false on error
 */
bool c64_shared_create_directory_recursive(const char *path)
{
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\')
        tmp[len - 1] = 0;

    // Start from the beginning, but skip drive letters on Windows (e.g., "C:")
    p = tmp;
    if (len > 1 && tmp[1] == ':') {
        p = tmp + 2; // Skip "C:" part on Windows
    }
    if (*p == '/' || *p == '\\') {
        p++; // Skip leading slash
    }

    // Create each directory in the path
    for (; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            if (os_mkdir(tmp) != 0) {
                // Check if it already exists (ignore error if it does)
                struct stat st;
                if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    // Directory creation failed and it doesn't exist
                    return false;
                }
            }
            *p = '/'; // Use forward slash consistently (works on Windows too)
        }
    }

    // Create the final directory
    if (os_mkdir(tmp) != 0) {
        struct stat st;
        if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    return true;
}

// Session management functions

/**
 * Ensure recording session exists, create timestamped folder if needed
 * @param context Source context containing session state
 */
void c64_session_ensure_exists(struct c64_source *context)
{
    // If session already exists, do nothing
    if (context->session_folder[0] != '\0') {
        return;
    }

    // Create new session folder with timestamp
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    time_t rawtime = timestamp_ms / 1000;
    struct tm *timeinfo = localtime(&rawtime);

    snprintf(context->session_folder, sizeof(context->session_folder), "%s/session_%04d%02d%02d_%02d%02d%02d",
             context->save_folder, timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour,
             timeinfo->tm_min, timeinfo->tm_sec);

    // Create the session directory recursively (cross-platform)
    C64_LOG_INFO("Attempting to create session directory: %s", context->session_folder);
    if (!c64_shared_create_directory_recursive(context->session_folder)) {
        C64_LOG_WARNING("Failed to create session directory: %s", context->session_folder);
        context->session_folder[0] = '\0'; // Clear on failure
    } else {
        C64_LOG_INFO("Successfully created recording session: %s", context->session_folder);
    }
}

/**
 * Check if any recording type is currently active
 * @param context Source context
 * @return true if frame saving or video recording is enabled
 */
bool c64_session_any_recording_active(struct c64_source *context)
{
    return context->save_frames || context->record_video;
}

/**
 * Stop CSV timing recording
 * @param context Source context
 */
void c64_stop_csv_recording(struct c64_source *context)
{
    if (context->timing_file) {
        fclose(context->timing_file);
        context->timing_file = NULL;
        context->csv_timing_base_ns = 0; // Reset timing base for next recording session
        C64_LOG_INFO("CSV timing recording stopped");
    }
}

/**
 * Stop network packet recording
 * @param context Source context
 */
void c64_stop_network_recording(struct c64_source *context)
{
    if (context->network_file) {
        fclose(context->network_file);
        context->network_file = NULL;
        context->network_timing_base_ns = 0; // Reset timing base for next recording session
        C64_LOG_INFO("Network packet recording stopped");
    }
}

/**
 * Clean up session if no recording is active
 * @param context Source context
 */
void c64_session_cleanup_if_needed(struct c64_source *context)
{
    if (!c64_session_any_recording_active(context)) {
        // Stop all recording when session ends
        c64_stop_csv_recording(context);
        c64_stop_network_recording(context);
        context->session_folder[0] = '\0';
        C64_LOG_INFO("Recording session ended");
    }
}

// Main entry point functions - delegate to appropriate modules

/**
 * Save single frame as BMP file (delegates to frames module)
 * @param context Source context
 * @param frame_buffer RGBA frame data
 */
void c64_save_frame_as_bmp(struct c64_source *context, uint32_t *frame_buffer)
{
    c64_frames_save_as_bmp(context, frame_buffer);
}

/**
 * Record video frame to AVI file (delegates to video module)
 * @param context Source context
 * @param frame_buffer RGBA frame data
 */
void c64_record_video_frame(struct c64_source *context, uint32_t *frame_buffer)
{
    c64_video_record_frame(context, frame_buffer);
}

/**
 * Record audio data to WAV file (delegates to audio module)
 * @param context Source context
 * @param audio_data PCM audio data
 * @param data_size Size of audio data in bytes
 */
void c64_record_audio_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size)
{
    c64_audio_record_data(context, audio_data, data_size);
}

/**
 * Start CSV timing recording for any recording type
 * @param context Source context
 */
void c64_start_csv_recording(struct c64_source *context)
{
    if (context->timing_file) {
        return; // Already recording CSV
    }

    // Ensure we have a recording session
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("Failed to create recording session for CSV logging");
        return;
    }

    // Create CSV timing file
    char timing_filename[950];
    snprintf(timing_filename, sizeof(timing_filename), "%s/obs.csv", context->session_folder);

    context->timing_file = fopen(timing_filename, "w");
    if (!context->timing_file) {
        C64_LOG_ERROR("Failed to create CSV timing file: %s", timing_filename);
        return;
    }

    // Write CSV header
    c64_obs_write_header(context);
    C64_LOG_INFO("Started CSV timing recording: %s", timing_filename);
}

/**
 * Start network packet recording for network analysis
 * @param context Source context
 */
void c64_start_network_recording(struct c64_source *context)
{
    if (context->network_file) {
        return; // Already recording network packets
    }

    // Ensure we have a recording session
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("Failed to create recording session for network logging");
        return;
    }

    // Create network packet file
    char network_filename[950];
    snprintf(network_filename, sizeof(network_filename), "%s/network.csv", context->session_folder);

    context->network_file = fopen(network_filename, "w");
    if (!context->network_file) {
        C64_LOG_ERROR("Failed to create network packet file: %s", network_filename);
        return;
    }

    // Write network CSV header
    c64_network_write_header(context);
    C64_LOG_INFO("Started network packet recording: %s", network_filename);
}

/**
 * Start video recording session (AVI + WAV + CSV)
 * @param context Source context
 */
void c64_start_video_recording(struct c64_source *context)
{
    if (!context->record_video || context->video_file) {
        return; // Already recording or not enabled
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Start CSV and network recording (creates session if needed)
    c64_start_csv_recording(context);
    c64_start_network_recording(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_ERROR("Failed to create recording session for video recording");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Create filenames in the session folder
    char video_filename[950], audio_filename[950];
    snprintf(video_filename, sizeof(video_filename), "%s/video.avi", context->session_folder);
    snprintf(audio_filename, sizeof(audio_filename), "%s/audio.wav", context->session_folder);

    // Open files for recording
    context->video_file = fopen(video_filename, "wb");
    context->audio_file = fopen(audio_filename, "wb");

    if (!context->video_file || !context->audio_file) {
        C64_LOG_ERROR("Failed to create recording files");
        if (context->video_file) {
            fclose(context->video_file);
            context->video_file = NULL;
        }
        if (context->audio_file) {
            fclose(context->audio_file);
            context->audio_file = NULL;
        }
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    context->recording_start_time = timestamp_ms;
    context->recorded_frames = 0;
    context->recorded_audio_samples = 0;

    // Write AVI header with detected frame rate
    c64_video_write_avi_header(context->video_file, context->width, context->height, context->expected_fps);

    // Write WAV header to audio file
    c64_audio_write_wav_header(context->audio_file, 48000, 2, 16); // 48kHz stereo 16-bit

    C64_LOG_INFO("Started video recording: %s", video_filename);

    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Stop video recording session and finalize files
 * @param context Source context
 */
void c64_stop_video_recording(struct c64_source *context)
{
    if (!context->video_file) {
        return; // Not recording
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Close recording files and finalize formats
    if (context->video_file) {
        fclose(context->video_file);
        context->video_file = NULL;
    }
    if (context->audio_file) {
        // Update WAV header with final file size
        // recorded_audio_samples counts stereo samples, each stereo sample = 4 bytes (16-bit L + 16-bit R)
        c64_audio_finalize_wav_header(context->audio_file, context->recorded_audio_samples * 4);
        fclose(context->audio_file);
        context->audio_file = NULL;
    }

    C64_LOG_INFO("Recording stopped. Frames: %u, Audio samples: %llu", context->recorded_frames,
                 (unsigned long long)context->recorded_audio_samples);

    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Initialize recording system state
 * @param context Source context to initialize
 */
void c64_record_init(struct c64_source *context)
{
    // Initialize recording fields
    context->save_frames = false;
    context->saved_frame_count = 0;
    memset(context->save_folder, 0, sizeof(context->save_folder));
    strncpy(context->save_folder, "./recordings", sizeof(context->save_folder) - 1);

    // Initialize video recording
    context->record_video = false;
    context->video_file = NULL;
    context->audio_file = NULL;
    context->timing_file = NULL;
    context->network_file = NULL;
    context->recording_start_time = 0;
    context->csv_timing_base_ns = 0;
    context->network_timing_base_ns = 0;
    context->recorded_frames = 0;
    context->recorded_audio_samples = 0;

    // Initialize recording mutex
    if (pthread_mutex_init(&context->recording_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize recording mutex");
    }
}

/**
 * Clean up recording system resources
 * @param context Source context to clean up
 */
void c64_record_cleanup(struct c64_source *context)
{
    // Stop recording if active
    if (context->record_video) {
        if (pthread_mutex_lock(&context->recording_mutex) == 0) {
            if (context->video_file) {
                fclose(context->video_file);
                context->video_file = NULL;
            }
            if (context->audio_file) {
                fclose(context->audio_file);
                context->audio_file = NULL;
            }
            // CSV file is handled by session cleanup
            pthread_mutex_unlock(&context->recording_mutex);
        }
    }

    // Clean up recording mutex
    pthread_mutex_destroy(&context->recording_mutex);
}

/**
 * Update recording settings from OBS properties
 * @param context Source context
 * @param settings_ptr OBS settings data
 */
void c64_record_update_settings(struct c64_source *context, void *settings_ptr)
{
    obs_data_t *settings = (obs_data_t *)settings_ptr;

    // Update frame saving settings
    const char *new_save_folder = obs_data_get_string(settings, "save_folder");
    if (new_save_folder && strlen(new_save_folder) > 0) {
        if (strcmp(context->save_folder, new_save_folder) != 0) {
            strncpy(context->save_folder, new_save_folder, sizeof(context->save_folder) - 1);
            context->save_folder[sizeof(context->save_folder) - 1] = '\0';
            context->saved_frame_count = 0; // Reset counter for new folder
            C64_LOG_INFO("Frame save folder updated: %s", context->save_folder);
        }
    }

    // Update frame saving (check if we need session cleanup)
    bool old_save_frames = context->save_frames;
    context->save_frames = obs_data_get_bool(settings, "save_frames");
    if (old_save_frames && !context->save_frames) {
        // Frame saving stopped, cleanup session if no other recording active
        c64_session_cleanup_if_needed(context);
    }

    // Update video recording settings
    bool new_record_video = obs_data_get_bool(settings, "record_video");
    if (new_record_video != context->record_video) {
        context->record_video = new_record_video;

        if (new_record_video) {
            // Start recording (will join/create session)
            c64_start_video_recording(context);
            C64_LOG_INFO("Video recording started");
        } else {
            // Stop recording
            c64_stop_video_recording(context);
            c64_session_cleanup_if_needed(context);
        }
    }
}
