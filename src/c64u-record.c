#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "c64u-logging.h"
#include "c64u-record.h"
#include "c64u-types.h"

// Helper function to write AVI header
static void write_avi_header(FILE *file, uint32_t width, uint32_t height, uint32_t fps)
{
    // Simple AVI header - we'll write a minimal structure for uncompressed BGR24
    // This is a simplified implementation for basic compatibility
    uint32_t file_size = 4096; // We'll update this later

    // RIFF header
    fwrite("RIFF", 1, 4, file);
    fwrite(&file_size, 4, 1, file);
    fwrite("AVI ", 1, 4, file);

    // AVI header chunk
    fwrite("LIST", 1, 4, file);
    uint32_t hdrl_size = 200;
    fwrite(&hdrl_size, 4, 1, file);
    fwrite("hdrl", 1, 4, file);

    // Main AVI header
    fwrite("avih", 1, 4, file);
    uint32_t avih_size = 56;
    fwrite(&avih_size, 4, 1, file);

    uint32_t frame_period = 1000000 / fps; // microseconds per frame
    fwrite(&frame_period, 4, 1, file);     // dwMicroSecPerFrame
    uint32_t zero = 0;
    fwrite(&zero, 4, 1, file); // dwMaxBytesPerSec
    fwrite(&zero, 4, 1, file); // dwPaddingGranularity
    fwrite(&zero, 4, 1, file); // dwFlags
    fwrite(&zero, 4, 1, file); // dwTotalFrames (update later)
    fwrite(&zero, 4, 1, file); // dwInitialFrames
    uint32_t one = 1;
    fwrite(&one, 4, 1, file);    // dwStreams
    fwrite(&zero, 4, 1, file);   // dwSuggestedBufferSize
    fwrite(&width, 4, 1, file);  // dwWidth
    fwrite(&height, 4, 1, file); // dwHeight
    fwrite(&zero, 4, 1, file);   // dwReserved[0]
    fwrite(&zero, 4, 1, file);   // dwReserved[1]
    fwrite(&zero, 4, 1, file);   // dwReserved[2]
    fwrite(&zero, 4, 1, file);   // dwReserved[3]
}

static void finalize_avi_header(FILE *file, uint32_t frame_count)
{
    if (!file)
        return;

    // Update total frames
    fseek(file, 48, SEEK_SET);
    fwrite(&frame_count, 4, 1, file);

    // Seek back to end
    fseek(file, 0, SEEK_END);
}

// Helper function to convert RGBA to BGR24
static void convert_rgba_to_bgr24(uint32_t *rgba_buffer, uint8_t *bgr_buffer, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < width * height; i++) {
        uint32_t pixel = rgba_buffer[i];
        bgr_buffer[i * 3 + 0] = (pixel >> 16) & 0xFF; // B
        bgr_buffer[i * 3 + 1] = (pixel >> 8) & 0xFF;  // G
        bgr_buffer[i * 3 + 2] = pixel & 0xFF;         // R
    }
}

// Helper function to write WAV file header
static void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;

    // WAV header (44 bytes) - we'll update sizes later
    fwrite("RIFF", 1, 4, file); // ChunkID
    uint32_t chunk_size = 36;   // ChunkSize (to be updated later)
    fwrite(&chunk_size, 4, 1, file);
    fwrite("WAVE", 1, 4, file);   // Format
    fwrite("fmt ", 1, 4, file);   // Subchunk1ID
    uint32_t subchunk1_size = 16; // Subchunk1Size (PCM)
    fwrite(&subchunk1_size, 4, 1, file);
    uint16_t audio_format = 1; // AudioFormat (PCM)
    fwrite(&audio_format, 2, 1, file);
    fwrite(&channels, 2, 1, file);        // NumChannels
    fwrite(&sample_rate, 4, 1, file);     // SampleRate
    fwrite(&byte_rate, 4, 1, file);       // ByteRate
    fwrite(&block_align, 2, 1, file);     // BlockAlign
    fwrite(&bits_per_sample, 2, 1, file); // BitsPerSample
    fwrite("data", 1, 4, file);           // Subchunk2ID
    uint32_t subchunk2_size = 0;          // Subchunk2Size (to be updated later)
    fwrite(&subchunk2_size, 4, 1, file);
}

// Helper function to finalize WAV header with correct file sizes
static void finalize_wav_header(FILE *file, uint32_t data_size)
{
    if (!file)
        return;

    // Update ChunkSize (file size - 8)
    fseek(file, 4, SEEK_SET);
    uint32_t chunk_size = data_size + 36;
    fwrite(&chunk_size, 4, 1, file);

    // Update Subchunk2Size (data size)
    fseek(file, 40, SEEK_SET);
    fwrite(&data_size, 4, 1, file);
}

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

    // Create timestamped session folder and filenames with standard formats
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    char session_folder[800], video_filename[900], audio_filename[900], timing_filename[900];

    // Create session folder: base_folder/session_YYYYMMDD_HHMMSS/
    time_t rawtime = timestamp_ms / 1000;
    struct tm *timeinfo = localtime(&rawtime);
    snprintf(session_folder, sizeof(session_folder), "%s/session_%04d%02d%02d_%02d%02d%02d", context->save_folder,
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
             timeinfo->tm_sec);

    snprintf(video_filename, sizeof(video_filename), "%s/video.avi", session_folder);
    snprintf(audio_filename, sizeof(audio_filename), "%s/audio.wav", session_folder);
    snprintf(timing_filename, sizeof(timing_filename), "%s/timing.txt", session_folder);

    // Create session directory if it doesn't exist
    char mkdir_cmd[1000];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", session_folder);
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

        // Write AVI header to video file (uncompressed RGB)
        write_avi_header(context->video_file, context->width, context->height, 50); // 50fps PAL

        // Write WAV header to audio file
        write_wav_header(context->audio_file, 48000, 2, 16); // 48kHz stereo 16-bit

        // Write header info to timing file
        fprintf(context->timing_file, "# C64U Video Recording Session\n");
        fprintf(context->timing_file, "# Session Folder: %s\n", session_folder);
        fprintf(context->timing_file, "# Start Time: %llu ms\n", (unsigned long long)timestamp_ms);
        fprintf(context->timing_file, "# Video Format: AVI (Uncompressed BGR24), %ux%u pixels @ 50fps\n",
                context->width, context->height);
        fprintf(context->timing_file, "# Audio Format: WAV PCM 48kHz 16-bit stereo\n");
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

    // Write AVI frame (convert RGBA to BGR24 for maximum compatibility)
    size_t frame_size = context->width * context->height * 3; // BGR24
    uint8_t *bgr_buffer = malloc(frame_size);
    if (bgr_buffer) {
        convert_rgba_to_bgr24(frame_buffer, bgr_buffer, context->width, context->height);
        size_t written = fwrite(bgr_buffer, 1, frame_size, context->video_file);
        free(bgr_buffer);

        if (written == frame_size) {
            uint64_t timestamp_ms = os_gettime_ns() / 1000000;

            // Log timing information
            if (context->timing_file) {
                fprintf(context->timing_file, "%u,%llu,%zu\n", context->recorded_frames,
                        (unsigned long long)timestamp_ms, frame_size);
                fflush(context->timing_file);
            }

            context->recorded_frames++;
        } else {
            C64U_LOG_WARNING("Failed to write video frame to recording");
        }
    } else {
        C64U_LOG_ERROR("Failed to allocate BGR conversion buffer");
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

    // Write audio data to WAV file (data is already in correct PCM format)
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

    // Close recording files and finalize formats
    if (context->video_file) {
        // Update AVI header with final frame count
        finalize_avi_header(context->video_file, context->recorded_frames);
        fclose(context->video_file);
        context->video_file = NULL;
    }
    if (context->audio_file) {
        // Update WAV header with final file size
        finalize_wav_header(context->audio_file, context->recorded_audio_samples * 2); // samples * 2 bytes
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
