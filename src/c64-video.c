#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include "c64-network.h" // Include network header first to avoid Windows header conflicts
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-video.h"
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

void c64_swap_frame_buffers(struct c64_source *context)
{
    // Save frame to disk if enabled (before swap to avoid race conditions)
    if (context->save_frames) {
        c64_save_frame_as_bmp(context, context->frame_buffer_back);
    }

    // Record frame to video file if recording is enabled
    if (context->record_video) {
        c64_record_video_frame(context, context->frame_buffer_back);
    }

    // Swap front and back buffers
    uint32_t *temp = context->frame_buffer_front;
    context->frame_buffer_front = context->frame_buffer_back;
    context->frame_buffer_back = temp;
    context->frame_ready = true;
    context->last_frame_time = os_gettime_ns(); // Update timestamp for timeout detection
    context->buffer_swap_pending = false;
}

void c64_assemble_frame_to_buffer(struct c64_source *context, struct frame_assembly *frame)
{
    // Assemble complete frame into back buffer according to C64 Ultimate specification
    // Each packet contains 4 lines of 384 pixels (192 bytes per line when packed as 4-bit)

    C64_LOG_DEBUG("🎬 Assembling frame %u: %u/%u packets received", frame->frame_num, frame->received_packets,
                  frame->expected_packets);

    for (int i = 0; i < C64_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        // Validate packet bounds
        if (lines_per_packet != C64_LINES_PER_PACKET) {
            C64_LOG_WARNING("❌ Packet %d has %u lines per packet (expected %u)", i, lines_per_packet,
                            C64_LINES_PER_PACKET);
        }

        // Process each line in the packet (should be 4 lines per packet)
        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
            uint32_t dst_line_offset = (line_num + line) * C64_PIXELS_PER_LINE;
            uint32_t *dst_line = context->frame_buffer_back + dst_line_offset;
            uint8_t *src_line = packet->packet_data + (line * C64_BYTES_PER_LINE);

            // Bounds checking
            uint32_t current_line = (uint32_t)(line_num + line);
            if (current_line >= context->height) {
                C64_LOG_WARNING("❌ Line %u exceeds frame height %u", current_line, context->height);
                break;
            }

            // Convert 4-bit VIC colors to 32-bit RGBA using optimized lookup table
            // Each source byte contains 2 pixels (4 bits each)
            c64_convert_pixels_optimized(src_line, dst_line, C64_BYTES_PER_LINE);
        }
    }

    C64_LOG_DEBUG("✅ Frame %u assembly complete", frame->frame_num);
}

// DEPRECATED: Legacy delay queue functions - stubbed out (network buffer replaces this functionality)

void c64_init_delay_queue(struct c64_source *context)
{
    // STUB: Network buffer initialization happens in c64_create()
    UNUSED_PARAMETER(context);
    C64_LOG_DEBUG("Legacy delay queue init called - using network buffer instead");
}

bool c64_enqueue_delayed_frame(struct c64_source *context, struct frame_assembly *frame, uint16_t sequence_num)
{
    // STUB: Frame enqueuing handled by network buffer
    UNUSED_PARAMETER(context);
    UNUSED_PARAMETER(frame);
    UNUSED_PARAMETER(sequence_num);
    return true; // Always return success to avoid breaking legacy code
}

bool c64_dequeue_delayed_frame(struct c64_source *context)
{
    // STUB: Frame dequeuing handled by network buffer in render function
    UNUSED_PARAMETER(context);
    return false; // Return false to indicate no legacy frames available
}

void c64_clear_delay_queue(struct c64_source *context)
{
    // STUB: Network buffer flushing handled by c64_network_buffer_flush()
    UNUSED_PARAMETER(context);
    C64_LOG_DEBUG("Legacy delay queue clear called - using network buffer instead");
}

// DUPLICATE FUNCTIONS REMOVED - See stub implementations above

// Performance optimization: Batch process video statistics to reduce hot path overhead
void c64_process_video_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    // Only process statistics every 5 seconds to minimize overhead
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL; // 5 seconds

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return; // Not time yet for statistics processing
    }

    // Read and reset counters (no atomics needed - single threaded access per counter type)
    uint64_t packets_received = context->video_packets_received;
    uint64_t bytes_received = context->video_bytes_received;
    uint32_t sequence_errors = context->video_sequence_errors;
    uint32_t frames_processed = context->video_frames_processed;
    context->video_packets_received = 0;
    context->video_bytes_received = 0;
    context->video_sequence_errors = 0;
    context->video_frames_processed = 0;

    // Calculate rates and statistics
    double duration_seconds = time_since_last_log / 1000000000.0;
    double packets_per_second = packets_received / duration_seconds;
    double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);
    double frames_per_second = frames_processed / duration_seconds;
    double loss_percentage = packets_received > 0 ? (100.0 * sequence_errors) / packets_received : 0.0;

    // Calculate frame delivery metrics
    double expected_fps = context->format_detected ? context->expected_fps : 50.0; // Default to PAL
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
                                      : 0.0; // Convert to ms

    // Log comprehensive statistics (only if we received packets)
    if (packets_received > 0) {
        C64_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", frames_per_second,
                     bandwidth_mbps, packets_per_second, loss_percentage, (uint32_t)frames_processed);
        C64_LOG_INFO("🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                     expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                     frame_completion_rate);
        C64_LOG_INFO(
            "📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms | Buffer swaps %u",
            capture_drop_pct, delivery_drop_pct, avg_pipeline_latency, context->buffer_swaps);
    }

    // Reset diagnostic counters and update timestamp
    context->frames_expected = 0;
    context->frames_captured = 0;
    context->frames_delivered_to_obs = 0;
    context->frames_completed = 0;
    context->buffer_swaps = 0;
    context->total_pipeline_latency = 0;
    context->last_stats_log_time = current_time;
}

// Performance optimization: Batch process audio statistics
void c64_process_audio_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL; // 5 seconds

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return; // Not time yet for statistics processing
    }

    // Read and reset audio counters
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

// Lock-free frame assembly optimization functions
void c64_init_frame_assembly_lockfree(struct frame_assembly *frame, uint16_t frame_num)
{
    // Initialize frame structure according to C64 Ultimate spec
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
    frame->received_packets = 0;
    frame->expected_packets = 0; // Will be set when we detect format or see last packet
    frame->complete = false;
    frame->packets_received_mask = 0;

    C64_LOG_DEBUG("🎬 Initialized frame assembly for frame %u", frame_num);
}

bool c64_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index)
{
    // Check if packet index is valid
    if (packet_index >= C64_MAX_PACKETS_PER_FRAME) {
        return false;
    }

    // Set the packet bit and increment counter
    uint64_t packet_mask = 1ULL << packet_index;
    uint64_t old_mask = frame->packets_received_mask;
    frame->packets_received_mask |= packet_mask;

    // If this bit was already set, this is a duplicate packet
    if (old_mask & packet_mask) {
        return false; // Duplicate packet
    }

    // Increment packet counter
    frame->received_packets++;

    return true; // Successfully added new packet
}

bool c64_is_frame_complete_lockfree(struct frame_assembly *frame)
{
    // Load current packet count
    uint16_t received = frame->received_packets;
    uint16_t expected = frame->expected_packets;

    // Frame must have expected packets set before we can check completion
    if (expected == 0) {
        return false; // Can't be complete if we don't know how many packets to expect
    }

    // Frame is complete when we have all expected packets
    bool complete = (received >= expected);

    // Debug logging for frame completion checks (throttled)
    static uint16_t last_debug_frame = 0;
    static uint64_t last_debug_time = 0;
    uint64_t now = os_gettime_ns();

    if (frame->frame_num != last_debug_frame && received > 0 &&
        (last_debug_time == 0 || (now - last_debug_time) > 1000000000ULL)) { // 1 second throttle
        C64_LOG_DEBUG("🎬 Frame completion check: frame %u has %u/%u packets (complete=%d)", frame->frame_num, received,
                      expected, complete);
        last_debug_frame = frame->frame_num;
        last_debug_time = now;
    }

    // Update completion flag if frame is complete
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
    // Windows: Increase thread priority for video receiver to reduce scheduling delays
    // High-frequency UDP packet reception (3400+ packets/sec) benefits from higher priority
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64_LOG_DEBUG("Set video receiver thread to above-normal priority on Windows");
    } else {
        C64_LOG_WARNING("Failed to set video receiver thread priority on Windows");
    }

    // Windows: Set thread to use high-resolution timing for better scheduling precision
    timeBeginPeriod(1); // Request 1ms timer resolution
#endif

    // Video receiver thread initialized
    C64_LOG_DEBUG("Video thread function started with optimized scheduling");

    while (context->thread_active) {
        ssize_t received = recv(context->video_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                // Windows: Use shorter sleep for high-frequency packet streams
                // C64 Ultimate sends 3400+ packets/sec, so 1ms sleep can miss multiple packets
                Sleep(0); // Yield to other threads, then retry immediately
                continue;
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay on Linux (usually works fine)
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

        // Update timestamp for timeout detection - UDP packet received successfully
        context->last_udp_packet_time = os_gettime_ns();

        // Update statistics counters (video thread only access)
        context->video_packets_received++;
        context->video_bytes_received += received;

        // Parse packet header (only what we need for validation)
        uint16_t seq_num = *(uint16_t *)(packet + 0);
        uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        uint8_t lines_per_packet = packet[8];
        uint8_t bits_per_pixel = packet[9];

        // Streamlined sequence error tracking
        static uint16_t last_video_seq = 0;
        static bool first_video = true;

        if (!first_video && seq_num != (uint16_t)(last_video_seq + 1)) {
            // Increment sequence error counter
            context->video_sequence_errors++;

            // Log sequence errors (keep for debugging, but move details out of hot path)
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

        // Batch process statistics every N packets or time interval (moved out of hot path)
        uint64_t now = os_gettime_ns();
        c64_process_video_statistics_batch(context, now);

        // Validate packet data
        if (lines_per_packet != C64_LINES_PER_PACKET || pixels_per_line != C64_PIXELS_PER_LINE || bits_per_pixel != 4) {
            C64_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet, pixels_per_line,
                            bits_per_pixel);
            continue;
        }

        // Push packet to network buffer for jitter correction and synchronization
        if (context->network_buffer) {
            c64_network_buffer_push_video(context->network_buffer, packet, received, now);
        } else {
            // If no network buffer, process packet immediately (fallback)
            c64_process_video_packet_direct(context, packet, received, now);
        }
    }

    C64_LOG_DEBUG("Video receiver thread stopped");

#ifdef _WIN32
    // Windows: Restore default timer resolution
    timeEndPeriod(1);
#endif

    return NULL;
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

    UNUSED_PARAMETER(seq_num); // Used for logging but not required for frame assembly

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
                        // Process frame immediately (no delay queue in network buffer mode)
                        if (context->last_completed_frame != context->current_frame.frame_num) {
                            if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                                c64_assemble_frame_to_buffer(context, &context->current_frame);
                                c64_swap_frame_buffers(context);
                                context->last_completed_frame = context->current_frame.frame_num;

                                // Track diagnostics (only once per completed frame!)
                                context->frames_completed++;
                                context->buffer_swaps++;
                                context->frames_delivered_to_obs++;
                                context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                                context->video_frames_processed++;

                                pthread_mutex_unlock(&context->frame_mutex);
                            }
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

                // Calculate expected FPS based on detected format
                if (frame_height == C64_PAL_HEIGHT) {
                    context->expected_fps = 50.125; // PAL: 50.125 Hz (actual C64 timing)
                    C64_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else if (frame_height == C64_NTSC_HEIGHT) {
                    context->expected_fps = 59.826; // NTSC: 59.826 Hz (actual C64 timing)
                    C64_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else {
                    // Unknown format, estimate based on packet count
                    context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
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

        // Check if frame is complete
        if (c64_is_frame_complete(&context->current_frame)) {
            // Process frame immediately (no delay queue in network buffer mode)
            if (context->last_completed_frame != context->current_frame.frame_num) {
                if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                    c64_assemble_frame_to_buffer(context, &context->current_frame);
                    c64_swap_frame_buffers(context);
                    context->last_completed_frame = context->current_frame.frame_num;

                    // Track diagnostics (only once per completed frame!)
                    context->frames_completed++;
                    context->buffer_swaps++;
                    context->frames_delivered_to_obs++;
                    context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                    context->video_frames_processed++;

                    pthread_mutex_unlock(&context->frame_mutex);
                }
            }

            // Reset for next frame
            c64_init_frame_assembly(&context->current_frame, 0);
        }

        pthread_mutex_unlock(&context->assembly_mutex);
    }
}

// Video processor thread - pulls packets from network buffer and processes them
void *c64_video_processor_thread_func(void *data)
{
    struct c64_source *context = data;

    C64_LOG_DEBUG("Video processor thread started");

    while (context->thread_active) {
        if (context->network_buffer) {
            const uint8_t *video_data, *audio_data;
            size_t video_size, audio_size;
            uint64_t timestamp_us;

            // Check for new synchronized packets
            if (c64_network_buffer_pop(context->network_buffer, &video_data, &video_size, &audio_data, &audio_size,
                                       &timestamp_us)) {

                // Process video packet if available
                if (video_data && video_size > 0) {
                    c64_process_video_packet_direct(context, video_data, video_size, timestamp_us * 1000);
                }

                // Process audio packet if available (TODO: implement audio processing)
                if (audio_data && audio_size > 0) {
                    // Audio processing would go here
                }

                // Update frame timing
                context->last_frame_time = os_gettime_ns();
            } else {
                // No packets available, brief sleep to avoid busy waiting
                os_sleep_ms(1);
            }
        } else {
            // No network buffer, sleep longer
            os_sleep_ms(10);
        }
    }

    C64_LOG_DEBUG("Video processor thread stopped");
    return NULL;
}
