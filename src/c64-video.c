#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include "c64-network.h"
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-video.h"
#include "c64-audio.h"
#include "c64-color.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-record.h"

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib") // For timeBeginPeriod/timeEndPeriod
#endif

#include "c64-protocol.h"

// Helper functions for frame assembly (updated to use lock-free implementation)
void c64_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    // Delegate to lock-free implementation for performance
    c64_init_frame_assembly_lockfree(frame, frame_num);
}

bool c64_is_frame_complete(struct frame_assembly *frame)
{
    // Delegate to lock-free implementation for performance
    return c64_is_frame_complete_lockfree(frame);
}

bool c64_is_frame_timeout(struct frame_assembly *frame)
{
    uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
    return elapsed > C64_FRAME_TIMEOUT_MS;
}

// Direct frame rendering with row interpolation for missing packets
void c64_render_frame_direct(struct c64_source *context, struct frame_assembly *frame, uint64_t timestamp_ns)
{
    // First, assemble the frame with interpolation for missing rows
    c64_assemble_frame_with_interpolation(context, frame);

    // Save frame to disk if enabled
    if (context->save_frames) {
        c64_save_frame_as_bmp(context, context->frame_buffer);
    }

    // Record frame to video file if recording is enabled
    if (context->record_video) {
        c64_record_video_frame(context, context->frame_buffer);
    }

    // DIRECT ASYNC VIDEO OUTPUT - no buffer swapping needed!
    struct obs_source_frame obs_frame = {0};

    // Set up frame data - RGBA format (4 bytes per pixel)
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = timestamp_ns; // Use packet timestamp for A/V sync
    obs_frame.flip = false;             // No vertical flip needed

    // Output frame directly to OBS - super efficient!
    obs_source_output_video(context->source, &obs_frame);

    // Update timing and status
    context->last_frame_time = timestamp_ns;
    context->frames_delivered_to_obs++;
    context->video_frames_processed++;

    C64_LOG_DEBUG("🎬 Direct async frame: %ux%u RGBA, timestamp=%" PRIu64 ", packets=%u/%u", obs_frame.width,
                  obs_frame.height, obs_frame.timestamp, frame->received_packets, frame->expected_packets);
}

// Simplified frame assembly with row interpolation for missing packets
void c64_assemble_frame_with_interpolation(struct c64_source *context, struct frame_assembly *frame)
{
    C64_LOG_DEBUG("🎬 Assembling frame %u with interpolation: %u/%u packets received", frame->frame_num,
                  frame->received_packets, frame->expected_packets);

    // Track which lines have been written
    bool *line_written = calloc(context->height, sizeof(bool));
    if (!line_written) {
        C64_LOG_ERROR("Failed to allocate line tracking array");
        return;
    }

    // First pass: assemble all received packets
    for (int i = 0; i < C64_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
            uint32_t current_line = line_num + line;
            if (current_line >= context->height)
                break;

            uint32_t dst_line_offset = current_line * C64_PIXELS_PER_LINE;
            uint32_t *dst_line = context->frame_buffer + dst_line_offset;
            uint8_t *src_line = packet->packet_data + (line * C64_BYTES_PER_LINE);

            c64_convert_pixels_optimized(src_line, dst_line, C64_BYTES_PER_LINE);
            line_written[current_line] = true;
        }
    }

    // Second pass: interpolate missing lines by duplicating the nearest valid line above
    for (uint32_t line = 0; line < context->height; line++) {
        if (!line_written[line]) {
            // Find nearest valid line above this one
            uint32_t source_line = 0;
            for (uint32_t search = line; search > 0; search--) {
                if (line_written[search - 1]) {
                    source_line = search - 1;
                    break;
                }
            }

            // Copy the source line to fill the gap
            uint32_t *dst = context->frame_buffer + (line * C64_PIXELS_PER_LINE);
            uint32_t *src = context->frame_buffer + (source_line * C64_PIXELS_PER_LINE);
            memcpy(dst, src, C64_PIXELS_PER_LINE * sizeof(uint32_t));
        }
    }

    free(line_written);
    C64_LOG_DEBUG("✅ Frame %u assembly with interpolation complete", frame->frame_num);
}

void c64_process_video_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = context->video_packets_received;
    uint64_t bytes_received = context->video_bytes_received;
    uint32_t sequence_errors = context->video_sequence_errors;
    uint32_t frames_processed = context->video_frames_processed;
    context->video_packets_received = 0;
    context->video_bytes_received = 0;
    context->video_sequence_errors = 0;
    context->video_frames_processed = 0;

    double duration_seconds = time_since_last_log / 1000000000.0;
    double packets_per_second = packets_received / duration_seconds;
    double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);
    double frames_per_second = frames_processed / duration_seconds;
    double loss_percentage = packets_received > 0 ? (100.0 * sequence_errors) / packets_received : 0.0;

    double expected_fps = context->format_detected ? context->expected_fps : 50.0;
    double frame_delivery_rate = context->frames_delivered_to_obs / duration_seconds;
    double frame_completion_rate = context->frames_completed / duration_seconds;
    double capture_drop_pct =
        context->frames_expected > 0
            ? (100.0 * (context->frames_expected - context->frames_captured)) / context->frames_expected
            : 0.0;
    double delivery_drop_pct =
        context->frames_completed > 0
            ? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) / context->frames_completed
            : 0.0;
    double avg_pipeline_latency = context->frames_delivered_to_obs > 0
                                      ? context->total_pipeline_latency / (context->frames_delivered_to_obs * 1000000.0)
                                      : 0.0;
    if (packets_received > 0) {
        C64_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", frames_per_second,
                     bandwidth_mbps, packets_per_second, loss_percentage, (uint32_t)frames_processed);
        C64_LOG_INFO("🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                     expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                     frame_completion_rate);
        C64_LOG_INFO("📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms",
                     capture_drop_pct, delivery_drop_pct, avg_pipeline_latency);
    }

    // Reset diagnostic counters and update timestamp
    context->frames_expected = 0;
    context->frames_captured = 0;
    context->frames_delivered_to_obs = 0;
    context->frames_completed = 0;
    context->total_pipeline_latency = 0;
    context->last_stats_log_time = current_time;
}

void c64_process_audio_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = context->audio_packets_received;
    uint64_t bytes_received = context->audio_bytes_received;
    context->audio_packets_received = 0;
    context->audio_bytes_received = 0;

    if (packets_received > 0) {
        double duration_seconds = time_since_last_log / 1000000000.0;
        double packets_per_second = packets_received / duration_seconds;
        double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);

        C64_LOG_INFO("🔊 AUDIO: %.2f Mbps | %.0f pps | Packets: %llu", bandwidth_mbps, packets_per_second,
                     (unsigned long long)packets_received);
    }
}

void c64_init_frame_assembly_lockfree(struct frame_assembly *frame, uint16_t frame_num)
{
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
    frame->received_packets = 0;
    frame->expected_packets = 0;
    frame->complete = false;
    frame->packets_received_mask = 0;

    C64_LOG_DEBUG("🎬 Initialized frame assembly for frame %u", frame_num);
}

bool c64_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index)
{
    if (packet_index >= C64_MAX_PACKETS_PER_FRAME) {
        return false;
    }

    uint64_t packet_mask = 1ULL << packet_index;
    uint64_t old_mask = frame->packets_received_mask;
    frame->packets_received_mask |= packet_mask;

    if (old_mask & packet_mask) {
        return false;
    }

    frame->received_packets++;
    return true;
}

bool c64_is_frame_complete_lockfree(struct frame_assembly *frame)
{
    uint16_t received = frame->received_packets;
    uint16_t expected = frame->expected_packets;

    if (expected == 0) {
        return false;
    }

    bool complete = (received >= expected);

    static uint16_t last_debug_frame = 0;
    static uint64_t last_debug_time = 0;
    uint64_t now = os_gettime_ns();

    if (frame->frame_num != last_debug_frame && received > 0 &&
        (last_debug_time == 0 || (now - last_debug_time) > 1000000000ULL)) {
        C64_LOG_DEBUG("🎬 Frame completion check: frame %u has %u/%u packets (complete=%d)", frame->frame_num, received,
                      expected, complete);
        last_debug_frame = frame->frame_num;
        last_debug_time = now;
    }

    if (complete && !frame->complete) {
        frame->complete = true;
        C64_LOG_DEBUG("🎬 Frame %u marked as COMPLETE with %u/%u packets!", frame->frame_num, received, expected);
    }

    return complete;
}

// Video thread function
void *c64_video_thread_func(void *data)
{
    struct c64_source *context = data;
    uint8_t packet[C64_VIDEO_PACKET_SIZE];

    C64_LOG_DEBUG("Video receiver thread started on port %u", context->video_port);

#ifdef _WIN32
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64_LOG_DEBUG("Set video receiver thread to above-normal priority on Windows");
    } else {
        C64_LOG_WARNING("Failed to set video receiver thread priority on Windows");
    }

    timeBeginPeriod(1);
#endif

    C64_LOG_DEBUG("Video thread function started with optimized scheduling");

    while (context->thread_active) {
        ssize_t received = recv(context->video_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                Sleep(0);
                continue;
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1);
                continue;
            }
#endif
            C64_LOG_ERROR("Video socket error: %s", c64_get_socket_error_string(error));
            break;
        }

        if (received != C64_VIDEO_PACKET_SIZE) {
            C64_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                            SSIZE_T_CAST(received), C64_VIDEO_PACKET_SIZE);
            continue;
        }

        context->last_udp_packet_time = os_gettime_ns();

        context->video_packets_received++;
        context->video_bytes_received += received;
        uint16_t seq_num = *(uint16_t *)(packet + 0);
        uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        uint8_t lines_per_packet = packet[8];
        uint8_t bits_per_pixel = packet[9];
        static uint16_t last_video_seq = 0;
        static bool first_video = true;

        if (!first_video && seq_num != (uint16_t)(last_video_seq + 1)) {
            context->video_sequence_errors++;

            uint16_t expected_seq = (uint16_t)(last_video_seq + 1);
            int16_t seq_diff = (int16_t)(seq_num - expected_seq);

            if (seq_diff > 0) {
                C64_LOG_WARNING("🔴 UDP OUT-OF-SEQUENCE: Expected seq %u, got %u (skipped %d packets)", expected_seq,
                                seq_num, seq_diff);
            } else {
                C64_LOG_WARNING("🔄 UDP OUT-OF-ORDER: Expected seq %u, got %u (reorder offset %d)", expected_seq,
                                seq_num, seq_diff);
            }
        }
        last_video_seq = seq_num;
        first_video = false;

        uint64_t now = os_gettime_ns();
        c64_process_video_statistics_batch(context, now);

        if (lines_per_packet != C64_LINES_PER_PACKET || pixels_per_line != C64_PIXELS_PER_LINE || bits_per_pixel != 4) {
            C64_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet, pixels_per_line,
                            bits_per_pixel);
            continue;
        }

        if (context->network_buffer) {
            c64_network_buffer_push_video(context->network_buffer, packet, received, now);
        } else {
            c64_process_video_packet_direct(context, packet, received, now);
        }
    }

    C64_LOG_DEBUG("Video receiver thread stopped");

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return NULL;
}

// Calculate ideal timestamp for a frame based on sequence number and video standard
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num)
{
    // Establish base timestamp on first frame
    if (!context->timestamp_base_set) {
        context->stream_start_time_ns = os_gettime_ns();
        context->first_frame_num = frame_num;
        context->timestamp_base_set = true;
        C64_LOG_INFO("📐 Established timestamp base: frame %u at %" PRIu64 " ns", frame_num,
                     context->stream_start_time_ns);
    }

    // Calculate frame offset from the first frame
    int32_t frame_offset = (int32_t)(frame_num - context->first_frame_num);

    // Handle sequence number wraparound (16-bit counter)
    if (frame_offset < -32768) {
        frame_offset += 65536; // Wrapped forward
    } else if (frame_offset > 32768) {
        frame_offset -= 65536; // Wrapped backward
    }

    // Calculate ideal timestamp: base + (frame_offset * frame_interval)
    uint64_t ideal_timestamp = context->stream_start_time_ns + ((uint64_t)frame_offset * context->frame_interval_ns);

    // Debug log occasionally to verify timestamp calculation
    static uint32_t log_counter = 0;
    if ((log_counter++ % 250) == 0) { // Log every 250 frames (~5 seconds at 50Hz)
        C64_LOG_DEBUG("📐 Ideal timestamp: frame %u (offset %d) = %" PRIu64 " ns", frame_num, frame_offset,
                      ideal_timestamp);
    }

    return ideal_timestamp;
}

// Direct packet processing function (based on main branch logic)
void c64_process_video_packet_direct(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns)
{
    if (!context || !packet || packet_size != C64_VIDEO_PACKET_SIZE) {
        return;
    }

    // Parse packet header (streamlined - only what we need for processing)
    uint16_t seq_num = *(uint16_t *)(packet + 0);
    uint16_t frame_num = *(uint16_t *)(packet + 2);
    uint16_t line_num = *(uint16_t *)(packet + 4);
    uint8_t lines_per_packet = packet[8];

    bool last_packet = (line_num & 0x8000) != 0;
    line_num &= 0x7FFF;

    // Process packet with frame assembly and double buffering (from main branch)
    if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
        // Track frame capture timing for diagnostics (per-frame, not per-packet)
        uint64_t capture_time = timestamp_ns;

        // Check if this is a new frame
        if (context->current_frame.frame_num != frame_num) {
            // Log frame transitions to detect skips and duplicates
            if (context->current_frame.frame_num != 0) {
                uint16_t expected_next = context->current_frame.frame_num + 1;
                int16_t frame_diff = (int16_t)(frame_num - expected_next);

                if (frame_diff > 0) {
                    C64_LOG_WARNING("📽️ FRAME SKIP: Expected frame %u, got %u (skipped %d frames)", expected_next,
                                    frame_num, frame_diff);
                } else if (frame_diff < 0) {
                    C64_LOG_WARNING("🔄 FRAME REVERT: Expected frame %u, got %u (went back %d frames)", expected_next,
                                    frame_num, -frame_diff);
                }
            }

            // Count expected and captured frames only on new frame start
            if (context->last_capture_time > 0) {
                context->frames_expected++;
            }
            context->frames_captured++;
            context->last_capture_time = capture_time;

            // Complete previous frame if it exists and is reasonably complete
            if (context->current_frame.received_packets > 0) {
                if (c64_is_frame_complete(&context->current_frame) || c64_is_frame_timeout(&context->current_frame)) {
                    if (c64_is_frame_complete(&context->current_frame)) {
                        if (context->last_completed_frame != context->current_frame.frame_num) {
                            // Calculate ideal timestamp for smooth OBS async video rendering
                            uint64_t ideal_timestamp =
                                c64_calculate_ideal_timestamp(context, context->current_frame.frame_num);

                            // Direct rendering - assembly and output with ideal timestamp!
                            c64_render_frame_direct(context, &context->current_frame, ideal_timestamp);
                            context->last_completed_frame = context->current_frame.frame_num;

                            // Track diagnostics
                            context->frames_completed++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                        }
                    } else {
                        // Frame timeout - log drop and continue
                        C64_LOG_WARNING("⏰ FRAME TIMEOUT: Frame %u timed out with %u/%u packets (%.1f%% complete)",
                                        context->current_frame.frame_num, context->current_frame.received_packets,
                                        context->current_frame.expected_packets,
                                        (context->current_frame.received_packets * 100.0f) /
                                            context->current_frame.expected_packets);
                        context->frame_drops++;
                    }
                }
            }

            // Initialize new frame
            c64_init_frame_assembly(&context->current_frame, frame_num);
        }

        // Add packet to current frame
        uint32_t packet_index = line_num / lines_per_packet;
        if (packet_index < C64_MAX_PACKETS_PER_FRAME) {
            struct frame_packet *fp = &context->current_frame.packets[packet_index];
            if (!fp->received) {
                fp->line_num = line_num;
                fp->lines_per_packet = lines_per_packet;
                fp->received = true;
                memcpy(fp->packet_data, packet + C64_VIDEO_HEADER_SIZE, C64_VIDEO_PACKET_SIZE - C64_VIDEO_HEADER_SIZE);
                context->current_frame.received_packets++;
            }
        } else {
            C64_LOG_WARNING("❌ INVALID PACKET: Frame %u, Line %u out of range (packet_index %u >= %d) - seq %u",
                            frame_num, line_num, packet_index, C64_MAX_PACKETS_PER_FRAME, seq_num);
            context->packet_drops++;
        }

        // Update expected packet count and detect video format based on last packet
        if (last_packet && context->current_frame.expected_packets == 0) {
            context->current_frame.expected_packets = packet_index + 1;

            // Detect PAL vs NTSC format from frame height
            uint32_t frame_height = line_num + lines_per_packet;
            if (!context->format_detected || context->detected_frame_height != frame_height) {
                context->detected_frame_height = frame_height;
                context->format_detected = true;

                // Calculate expected FPS and frame interval based on detected format
                if (frame_height == C64_PAL_HEIGHT) {
                    context->expected_fps = 50.125;
                    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS;
                    C64_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else if (frame_height == C64_NTSC_HEIGHT) {
                    context->expected_fps = 59.826;
                    context->frame_interval_ns = C64_NTSC_FRAME_INTERVAL_NS;
                    C64_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else {
                    // Unknown format, estimate based on packet count
                    context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
                    context->frame_interval_ns = (frame_height <= 250) ? C64_NTSC_FRAME_INTERVAL_NS
                                                                       : C64_PAL_FRAME_INTERVAL_NS;
                    C64_LOG_WARNING("⚠️ Unknown video format: 384x%u, assuming %.3f Hz", frame_height,
                                    context->expected_fps);
                }

                // Update context dimensions if they changed
                if (context->height != frame_height) {
                    context->height = frame_height;
                    context->width = C64_PIXELS_PER_LINE; // Always 384
                }
            }
        }

        if (c64_is_frame_complete(&context->current_frame)) {
            if (context->last_completed_frame != context->current_frame.frame_num) {
                // Direct rendering - assembly and output in one call!
                c64_render_frame_direct(context, &context->current_frame, capture_time);
                context->last_completed_frame = context->current_frame.frame_num;

                // Track diagnostics
                context->frames_completed++;
                context->total_pipeline_latency += (os_gettime_ns() - capture_time);
            }

            // Reset for next frame
            c64_init_frame_assembly(&context->current_frame, 0);
        }

        pthread_mutex_unlock(&context->assembly_mutex);
    }
}

// Render logo frame for async video output when no C64 connection
void c64_render_logo_frame(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context->frame_buffer) {
        return;
    }

    // Create a simple logo pattern: dark blue background with C64 blue borders
    const uint32_t bg_color = 0xFF000080;     // Dark blue background (RGBA)
    const uint32_t border_color = 0xFF4040FF; // C64 blue border (RGBA)
    const uint32_t text_color = 0xFFFFFFFF;   // White text (RGBA)

    uint32_t *buffer = context->frame_buffer;
    uint32_t width = context->width;
    uint32_t height = context->height;

    // Fill background
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t color = bg_color;

            // Add borders (top/bottom 8 pixels, left/right 16 pixels)
            if (y < 8 || y >= height - 8 || x < 16 || x >= width - 16) {
                color = border_color;
            }

            // Add centered text region (simple pattern for "C64 STREAM")
            uint32_t center_x = width / 2;
            uint32_t center_y = height / 2;
            uint32_t text_width = 160; // Approximate text width
            uint32_t text_height = 24; // Text height

            if (x >= center_x - text_width / 2 && x < center_x + text_width / 2 && y >= center_y - text_height / 2 &&
                y < center_y + text_height / 2) {
                // Simple text pattern - every 8th pixel horizontally, every 2nd row vertically
                if (((y - (center_y - text_height / 2)) % 4 == 0 || (y - (center_y - text_height / 2)) % 4 == 1) &&
                    (x - (center_x - text_width / 2)) % 8 < 6) {
                    color = text_color;
                }
            }

            buffer[y * width + x] = color;
        }
    }

    // Output logo frame via async video
    struct obs_source_frame obs_frame = {0};
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = timestamp_ns;
    obs_frame.flip = false;

    obs_source_output_video(context->source, &obs_frame);

    C64_LOG_DEBUG("🔷 Logo frame rendered: %ux%u RGBA, timestamp=%" PRIu64, obs_frame.width, obs_frame.height,
                  obs_frame.timestamp);
}

void *c64_video_processor_thread_func(void *data)
{
    struct c64_source *context = data;
    uint64_t last_logo_frame_time = 0;
    const uint64_t logo_frame_interval_ns = 20000000ULL; // 50Hz (20ms) for logo frames

    C64_LOG_DEBUG("Video processor thread started");

    while (context->thread_active) {
        uint64_t current_time = os_gettime_ns();
        bool packet_processed = false;

        if (context->network_buffer) {
            const uint8_t *video_data, *audio_data;
            size_t video_size, audio_size;
            uint64_t timestamp_us;

            if (c64_network_buffer_pop(context->network_buffer, &video_data, &video_size, &audio_data, &audio_size,
                                       &timestamp_us)) {

                if (video_data && video_size > 0) {
                    c64_process_video_packet_direct(context, video_data, video_size, timestamp_us * 1000);
                }

                if (audio_data && audio_size > 0) {
                    c64_process_audio_packet(context, audio_data, audio_size, timestamp_us * 1000);
                }

                context->last_frame_time = current_time;
                packet_processed = true;
            }
        }

        // If no packets processed and enough time has passed, show logo at 50Hz
        if (!packet_processed) {
            uint64_t time_since_last_frame = current_time - context->last_frame_time;
            uint64_t time_since_last_logo = current_time - last_logo_frame_time;

            // Show logo if no frames for 100ms AND we haven't shown logo recently
            if (time_since_last_frame > 100000000ULL && time_since_last_logo >= logo_frame_interval_ns) {
                c64_render_logo_frame(context, current_time);
                last_logo_frame_time = current_time;
                context->last_frame_time = current_time;
            }
            os_sleep_ms(1);
        }
    }

    C64_LOG_DEBUG("Video processor thread stopped");
    return NULL;
}
