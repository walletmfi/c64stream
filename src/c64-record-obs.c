/**
 * OBS timing recording for C64 Ultimate streaming
 * Provides detailed timing analysis for debugging audio/video synchronization
 */

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "c64-logging.h"
#include "c64-types.h"
#include "c64-record.h"
#include "c64-record-obs.h"

/**
 * Write OBS timing CSV header
 * Initializes the CSV file with column headers for timing analysis
 * @param context Source context with valid timing file handle
 */
void c64_obs_write_header(struct c64_source *context)
{
    if (!context || !context->timing_file) {
        C64_LOG_ERROR("Cannot write CSV header: context or timing file is NULL");
        return;
    }

    // Set timing base to current time for microsecond calculations
    context->csv_timing_base_ns = os_gettime_ns();

    // Write CSV header with all timing columns
    fprintf(
        context->timing_file,
        "event_type,frame_num,elapsed_us,calculated_timestamp_ms,actual_timestamp_ms,data_size_bytes,fps,audio_samples_total,video_packets_received,audio_packets_received,sequence_errors\n");
    fflush(context->timing_file);

    C64_LOG_INFO("OBS timing CSV header written successfully");
}

/**
 * Log video frame timing event to OBS CSV
 * @param context Source context
 * @param calculated_timestamp_ms Expected timestamp based on frame timing
 * @param actual_timestamp_ms Actual timestamp when frame was processed
 * @param frame_size Size of frame data in bytes
 */
void c64_obs_log_video_event(struct c64_source *context, uint64_t calculated_timestamp_ms, uint64_t actual_timestamp_ms,
                             size_t frame_size)
{
    if (!context || !context->timing_file) {
        return; // Silently ignore if timing file not available
    }

    // Calculate elapsed microseconds since CSV timing started
    uint64_t current_ns = os_gettime_ns();
    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Write video timing event to CSV
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    fprintf(context->timing_file, "video,%u,%llu,%llu,%llu,%zu,%.3f,%u,%llu,%llu,%llu\n", context->recorded_frames,
            (unsigned long long)elapsed_us, (unsigned long long)calculated_timestamp_ms,
            (unsigned long long)actual_timestamp_ms, frame_size, context->expected_fps, context->recorded_audio_samples,
            (unsigned long long)video_packets, (unsigned long long)audio_packets, (unsigned long long)sequence_errors);

    // Flush immediately for real-time analysis
    fflush(context->timing_file);
}

/**
 * Log audio data timing event to OBS CSV
 * @param context Source context
 * @param calculated_timestamp_ms Expected timestamp based on audio timing
 * @param actual_timestamp_ms Actual timestamp when audio was processed
 * @param data_size Size of audio data in bytes
 */
void c64_obs_log_audio_event(struct c64_source *context, uint64_t calculated_timestamp_ms, uint64_t actual_timestamp_ms,
                             size_t data_size)
{
    if (!context || !context->timing_file) {
        return; // Silently ignore if timing file not available
    }

    // Calculate elapsed microseconds since CSV timing started
    uint64_t current_ns = os_gettime_ns();
    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Write audio timing event to CSV
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    fprintf(context->timing_file, "audio,0,%llu,%llu,%llu,%zu,%.3f,%u,%llu,%llu,%llu\n", (unsigned long long)elapsed_us,
            (unsigned long long)calculated_timestamp_ms, (unsigned long long)actual_timestamp_ms, data_size,
            context->expected_fps, context->recorded_audio_samples, (unsigned long long)video_packets,
            (unsigned long long)audio_packets, (unsigned long long)sequence_errors);

    // Flush immediately for real-time analysis
    fflush(context->timing_file);
}
