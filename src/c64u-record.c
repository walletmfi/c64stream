#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "c64u-logging.h"
#include "c64u-record.h"
#include "c64u-types.h"

// Fast BMP file saving function (minimal CPU overhead)
void save_frame_as_bmp(struct c64u_source *context, uint32_t *frame_buffer)
{
    if (!context->save_frames || !frame_buffer) {
        return;
    }

    // Create timestamped filename
    uint64_t timestamp_ms = os_gettime_ns() / 1000000; // Convert to milliseconds
    char filename[768];
    snprintf(filename, sizeof(filename), "%s/frame_%llu_%05u.bmp", context->save_folder,
             (unsigned long long)timestamp_ms, context->saved_frame_count++);

    // Create directory if it doesn't exist (simple approach)
    char mkdir_cmd[800];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", context->save_folder);
    int result = system(mkdir_cmd);
    if (result != 0) {
        C64U_LOG_WARNING("Failed to create save directory: %s", context->save_folder);
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        C64U_LOG_WARNING("Failed to create frame file: %s", filename);
        return;
    }

    uint32_t width = context->width;
    uint32_t height = context->height;
    uint32_t row_padded = (width * 3 + 3) & (~3); // 4-byte alignment for BMP
    uint32_t image_size = row_padded * height;
    uint32_t file_size = 54 + image_size; // BMP header + image data

    // BMP Header (54 bytes total)
    uint8_t header[54] = {
        'B',
        'M', // Signature
        file_size & 0xFF,
        (file_size >> 8) & 0xFF,
        (file_size >> 16) & 0xFF,
        (file_size >> 24) & 0xFF, // File size
        0,
        0,
        0,
        0, // Reserved
        54,
        0,
        0,
        0, // Data offset
        40,
        0,
        0,
        0, // Header size
        width & 0xFF,
        (width >> 8) & 0xFF,
        (width >> 16) & 0xFF,
        (width >> 24) & 0xFF, // Width
        height & 0xFF,
        (height >> 8) & 0xFF,
        (height >> 16) & 0xFF,
        (height >> 24) & 0xFF, // Height
        1,
        0, // Planes
        24,
        0, // Bits per pixel
        0,
        0,
        0,
        0, // Compression
        image_size & 0xFF,
        (image_size >> 8) & 0xFF,
        (image_size >> 16) & 0xFF,
        (image_size >> 24) & 0xFF, // Image size
        0,
        0,
        0,
        0, // X pixels per meter
        0,
        0,
        0,
        0, // Y pixels per meter
        0,
        0,
        0,
        0, // Colors used
        0,
        0,
        0,
        0 // Colors important
    };

    fwrite(header, 1, 54, file);

    // Write image data (BMP stores bottom-to-top, convert RGBA to BGR)
    uint8_t *row_buffer = malloc(row_padded);
    if (row_buffer) {
        for (int y = height - 1; y >= 0; y--) { // Bottom-to-top
            uint32_t *src_row = frame_buffer + (y * width);
            uint8_t *dst = row_buffer;

            for (uint32_t x = 0; x < width; x++) {
                uint32_t pixel = src_row[x];
                *dst++ = (pixel >> 16) & 0xFF; // B
                *dst++ = (pixel >> 8) & 0xFF;  // G
                *dst++ = pixel & 0xFF;         // R
            }

            // Pad to 4-byte boundary
            while ((dst - row_buffer) % 4 != 0) {
                *dst++ = 0;
            }

            fwrite(row_buffer, 1, row_padded, file);
        }
        free(row_buffer);
    }

    fclose(file);
}

// Video recording functions (raw uncompressed format for minimal CPU overhead)
void start_video_recording(struct c64u_source *context)
{
    if (!context->record_video || context->video_file) {
        return; // Already recording or not enabled
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Create timestamped filenames
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    char video_filename[768], audio_filename[768], timing_filename[768];

    snprintf(video_filename, sizeof(video_filename), "%s/video_%llu.raw", context->save_folder,
             (unsigned long long)timestamp_ms);
    snprintf(audio_filename, sizeof(audio_filename), "%s/audio_%llu.raw", context->save_folder,
             (unsigned long long)timestamp_ms);
    snprintf(timing_filename, sizeof(timing_filename), "%s/timing_%llu.txt", context->save_folder,
             (unsigned long long)timestamp_ms);

    // Create directory if it doesn't exist
    char mkdir_cmd[800];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", context->save_folder);
    int result = system(mkdir_cmd);
    if (result != 0) {
        C64U_LOG_WARNING("Failed to create recording directory: %s", context->save_folder);
    }

    // Open files for recording
    context->video_file = fopen(video_filename, "wb");
    context->audio_file = fopen(audio_filename, "wb");
    context->timing_file = fopen(timing_filename, "w");

    if (!context->video_file || !context->audio_file || !context->timing_file) {
        C64U_LOG_ERROR("Failed to create recording files");
        if (context->video_file) {
            fclose(context->video_file);
            context->video_file = NULL;
        }
        if (context->audio_file) {
            fclose(context->audio_file);
            context->audio_file = NULL;
        }
        if (context->timing_file) {
            fclose(context->timing_file);
            context->timing_file = NULL;
        }
    } else {
        context->recording_start_time = timestamp_ms;
        context->recorded_frames = 0;
        context->recorded_audio_samples = 0;

        // Write header info to timing file
        fprintf(context->timing_file, "# C64U Raw Video Recording\n");
        fprintf(context->timing_file, "# Start Time: %llu ms\n", (unsigned long long)timestamp_ms);
        fprintf(context->timing_file, "# Video Format: Raw RGBA, %ux%u pixels per frame\n", context->width,
                context->height);
        fprintf(context->timing_file, "# Audio Format: Raw 16-bit signed PCM\n");
        fprintf(context->timing_file, "# Columns: frame_number, timestamp_ms, frame_size_bytes\n");
        fflush(context->timing_file);

        C64U_LOG_INFO("Started video recording: %s", video_filename);
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void record_video_frame(struct c64u_source *context, uint32_t *frame_buffer)
{
    if (!context->record_video || !context->video_file || !frame_buffer) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Write raw RGBA frame data (no compression, minimal CPU overhead)
    size_t frame_size = context->width * context->height * 4; // RGBA
    size_t written = fwrite(frame_buffer, 1, frame_size, context->video_file);

    if (written == frame_size) {
        uint64_t timestamp_ms = os_gettime_ns() / 1000000;

        // Log timing information
        if (context->timing_file) {
            fprintf(context->timing_file, "%u,%llu,%zu\n", context->recorded_frames, (unsigned long long)timestamp_ms,
                    frame_size);
            fflush(context->timing_file);
        }

        context->recorded_frames++;
    } else {
        C64U_LOG_WARNING("Failed to write video frame to recording");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void record_audio_data(struct c64u_source *context, const uint8_t *audio_data, size_t data_size)
{
    if (!context->record_video || !context->audio_file || !audio_data) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Write raw audio data (no compression, minimal CPU overhead)
    size_t written = fwrite(audio_data, 1, data_size, context->audio_file);

    if (written == data_size) {
        context->recorded_audio_samples += data_size / 2; // Assuming 16-bit samples
    } else {
        C64U_LOG_WARNING("Failed to write audio data to recording");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void stop_video_recording(struct c64u_source *context)
{
    if (!context->video_file) {
        return; // Not recording
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Close all recording files
    if (context->video_file) {
        fclose(context->video_file);
        context->video_file = NULL;
    }
    if (context->audio_file) {
        fclose(context->audio_file);
        context->audio_file = NULL;
    }
    if (context->timing_file) {
        fclose(context->timing_file);
        context->timing_file = NULL;
    }

    C64U_LOG_INFO("Recording stopped. Frames: %u, Audio samples: %llu", context->recorded_frames,
                  (unsigned long long)context->recorded_audio_samples);

    pthread_mutex_unlock(&context->recording_mutex);
}

// Recording initialization function
void c64u_record_init(struct c64u_source *context)
{
    // Initialize recording fields
    context->save_frames = false;
    context->saved_frame_count = 0;
    memset(context->save_folder, 0, sizeof(context->save_folder));
    strncpy(context->save_folder, "/tmp/c64u_frames", sizeof(context->save_folder) - 1);

    // Initialize video recording
    context->record_video = false;
    context->video_file = NULL;
    context->audio_file = NULL;
    context->timing_file = NULL;
    context->recording_start_time = 0;
    context->recorded_frames = 0;
    context->recorded_audio_samples = 0;

    // Initialize recording mutex
    if (pthread_mutex_init(&context->recording_mutex, NULL) != 0) {
        C64U_LOG_ERROR("Failed to initialize recording mutex");
    }
}

// Recording cleanup function
void c64u_record_cleanup(struct c64u_source *context)
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
            if (context->timing_file) {
                fclose(context->timing_file);
                context->timing_file = NULL;
            }
            pthread_mutex_unlock(&context->recording_mutex);
        }
    }

    // Clean up recording mutex
    pthread_mutex_destroy(&context->recording_mutex);
}

// Recording settings update function
void c64u_record_update_settings(struct c64u_source *context, void *settings_ptr)
{
    obs_data_t *settings = (obs_data_t *)settings_ptr;

    // Update frame saving settings
    context->save_frames = obs_data_get_bool(settings, "save_frames");
    const char *new_save_folder = obs_data_get_string(settings, "save_folder");
    if (new_save_folder && strlen(new_save_folder) > 0) {
        if (strcmp(context->save_folder, new_save_folder) != 0) {
            strncpy(context->save_folder, new_save_folder, sizeof(context->save_folder) - 1);
            context->save_folder[sizeof(context->save_folder) - 1] = '\0';
            context->saved_frame_count = 0; // Reset counter for new folder
            C64U_LOG_INFO("Frame save folder updated: %s", context->save_folder);
        }
    }

    // Update video recording settings
    bool new_record_video = obs_data_get_bool(settings, "record_video");
    if (new_record_video != context->record_video) {
        context->record_video = new_record_video;

        if (new_record_video) {
            // Start recording
            start_video_recording(context);
            C64U_LOG_INFO("Video recording started");
        } else {
            // Stop recording
            stop_video_recording(context);
        }
    }
}