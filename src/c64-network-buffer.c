#include "c64-network-buffer.h"
#include "c64-logging.h"
#include <obs-module.h>
#include <util/platform.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

struct packet_slot {
    uint8_t data[C64_VIDEO_PACKET_SIZE > C64_AUDIO_PACKET_SIZE ? C64_VIDEO_PACKET_SIZE : C64_AUDIO_PACKET_SIZE];
    size_t size;
    uint64_t timestamp_us;
    uint16_t sequence_num;
    bool valid; // Indicates if this slot contains valid data
};

typedef enum { BUFFER_TYPE_VIDEO, BUFFER_TYPE_AUDIO } buffer_type_t;

struct packet_ring_buffer {
    struct packet_slot *slots;  // Points to either video or audio slot array
    size_t max_capacity;        // Either C64_MAX_VIDEO_PACKETS or C64_MAX_AUDIO_PACKETS
    size_t active_slots;        // computed from delay
    size_t head;                // Protected by mutex
    size_t tail;                // Protected by mutex
    size_t packet_size;         // Either C64_VIDEO_PACKET_SIZE or C64_AUDIO_PACKET_SIZE
    uint16_t next_expected_seq; // Expected next sequence number
    bool seq_initialized;       // Whether we have initialized sequence tracking
    uint64_t delay_us;          // Delay in microseconds
    buffer_type_t type;         // For logging and type-specific behavior
    pthread_mutex_t mutex;      // Synchronizes access to head, tail, and slots
};

struct c64_network_buffer {
    struct packet_ring_buffer video;
    struct packet_ring_buffer audio;
    // Storage for the actual slot arrays
    struct packet_slot video_slots[C64_MAX_VIDEO_PACKETS];
    struct packet_slot audio_slots[C64_MAX_AUDIO_PACKETS];
};

// ----------------------------------
// Internal helpers
// ----------------------------------

// Debug function to verify buffer sequence ordering
static void debug_verify_buffer_ordering(struct packet_ring_buffer *rb, const char *context)
{
#ifdef DEBUG_SEQUENCE_ORDERING
    // Note: This function assumes caller already holds the mutex
    size_t head = rb->head;
    size_t tail = rb->tail;

    if (head == tail)
        return; // Empty buffer

    uint16_t prev_seq = 0;
    bool first = true;
    size_t current = tail;

    while (current != head) {
        if (rb->slots[current].valid) {
            uint16_t seq = rb->slots[current].sequence_num;
            if (!first) {
                int16_t diff = (int16_t)(seq - prev_seq);
                if (diff <= 0) {
                    C64_LOG_ERROR("%s: Buffer ordering violation - seq %u after %u at pos %zu", context, seq, prev_seq,
                                  current);
                }
            }
            prev_seq = seq;
            first = false;
        }
        current = (current + 1) % rb->max_capacity;
    }
#else
    // Avoid unused parameter warnings in release builds
    (void)rb;
    (void)context;
#endif
}

// Generic ring buffer operations using macros to work with both buffer types

// Generic packet ring buffer push with sequence-based ordering (OPTIMIZED FOR LOW LATENCY)
static void rb_push(struct packet_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    if (!rb || !data || len < 2) { // Need at least 2 bytes for sequence number
        return;
    }

    // Extract sequence number from packet header (first 2 bytes, little-endian)
    uint16_t seq_num = *(uint16_t *)(data);

    const char *type_name = (rb->type == BUFFER_TYPE_VIDEO) ? "Video" : "Audio";

    // Initialize sequence tracking on first packet
    if (!rb->seq_initialized) {
        rb->next_expected_seq = seq_num;
        rb->seq_initialized = true;
        C64_LOG_DEBUG("%s buffer: initialized with sequence %u", type_name, seq_num);
    }

    // Extremely simple approach: allow all packets through, let the buffer handle ordering
    // The ring buffer insertion sort will handle duplicates and ordering automatically

    // Find insertion point to maintain sequence order
    pthread_mutex_lock(&rb->mutex);
    size_t head = rb->head;
    size_t tail = rb->tail;

    // Check if buffer is full
    size_t next_head = (head + 1) % rb->max_capacity;
    if (next_head == tail) {
        // Buffer full: drop oldest packet
        rb->tail = (tail + 1) % rb->max_capacity;
        tail = rb->tail;
    }

    // CRITICAL OPTIMIZATION: Limit insertion sort complexity to prevent blocking
    size_t insert_pos = head;

    // For real-time performance, only do limited insertion sorting
    // Audio packets arrive more frequently, so use smaller search depth
    const size_t MAX_SEARCH_DEPTH = (rb->type == BUFFER_TYPE_VIDEO) ? 8 : 6;
    size_t search_depth = 0;
    bool found_insert_pos = false;

    // Always do insertion sort to maintain sequence order
    {
        // Limited backward search for insertion point
        size_t current = head;
        while (current != tail && search_depth < MAX_SEARCH_DEPTH) {
            size_t prev = (current == 0) ? rb->max_capacity - 1 : current - 1;
            if (prev == tail)
                break;

            if (!rb->slots[prev].valid) {
                current = prev;
                search_depth++;
                continue;
            }

            // Compare sequence numbers with wraparound handling
            int16_t seq_diff = (int16_t)(seq_num - rb->slots[prev].sequence_num);
            if (seq_diff >= 0) {
                // Found correct insertion point
                insert_pos = current;
                found_insert_pos = true;
                break;
            }

            current = prev;
            insert_pos = current;
            search_depth++;
        }
    }

    // Only do expensive shift operation if we found insertion point within search limit
    if (found_insert_pos && insert_pos != head) {
        // Limited shift operation to prevent blocking
        size_t shift_pos = head;
        size_t shift_count = 0;
        const size_t MAX_SHIFT_COUNT = (rb->type == BUFFER_TYPE_VIDEO) ? 8 : 6; // Audio has smaller limit

        while (shift_pos != insert_pos && shift_count < MAX_SHIFT_COUNT) {
            size_t prev = (shift_pos == 0) ? rb->max_capacity - 1 : shift_pos - 1;
            rb->slots[shift_pos] = rb->slots[prev];
            shift_pos = prev;
            shift_count++;
        }

        if (shift_count >= MAX_SHIFT_COUNT) {
            // Shift limit exceeded - insert at head to avoid blocking (packet may be slightly out of order)
            insert_pos = head;
            C64_LOG_DEBUG("%s: Shift limit exceeded for seq %u, inserting at head", type_name, seq_num);
        }
    }

    // Insert the new packet
    size_t copy_len = len < rb->packet_size ? len : rb->packet_size;
    memcpy(rb->slots[insert_pos].data, data, copy_len);

    // Zero-pad if packet is smaller than expected size
    if (copy_len < rb->packet_size) {
        memset(rb->slots[insert_pos].data + copy_len, 0, rb->packet_size - copy_len);
    }

    rb->slots[insert_pos].size = rb->packet_size;
    rb->slots[insert_pos].timestamp_us = ts;
    rb->slots[insert_pos].sequence_num = seq_num;
    rb->slots[insert_pos].valid = true;

    // No complex sequence tracking needed - let the pop function handle this

    // Log sequence insertion details for debugging
    if (insert_pos != head) {
        C64_LOG_DEBUG("%s: Inserted seq %u at pos %zu (head was %zu)", type_name, seq_num, insert_pos, head);
    }

    rb->head = next_head;
    pthread_mutex_unlock(&rb->mutex);

    // Verify buffer ordering in debug builds
    debug_verify_buffer_ordering(rb, type_name);
}

// Pop the oldest packet (FIFO order) - essential for proper frame assembly
static int rb_pop_oldest(struct packet_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    pthread_mutex_lock(&rb->mutex);
    size_t head = rb->head;
    size_t tail = rb->tail;

    if (head == tail) {
        // No packets available
        return 0;
    }

    *out = &rb->slots[tail];

    // Update expected sequence to track what we've consumed
    if (rb->slots[tail].valid && rb->seq_initialized) {
        uint16_t popped_seq = rb->slots[tail].sequence_num;
        rb->next_expected_seq = popped_seq + 1;
    }

    rb->tail = (tail + 1) % rb->max_capacity;
    pthread_mutex_unlock(&rb->mutex);
    return 1;
}

// Reset buffer with new active slot count
static void rb_reset(struct packet_ring_buffer *rb, size_t active_slots)
{
    if (!rb) {
        return;
    }

    pthread_mutex_lock(&rb->mutex);
    if (active_slots > rb->max_capacity) {
        active_slots = rb->max_capacity;
    }
    rb->active_slots = active_slots;
    rb->head = 0;
    rb->tail = 0;

    // Reset sequence tracking
    rb->next_expected_seq = 0;
    rb->seq_initialized = false;

    // Clear all slot validity
    for (size_t i = 0; i < rb->max_capacity; i++) {
        rb->slots[i].valid = false;
    }
    pthread_mutex_unlock(&rb->mutex);
}

// ----------------------------------
// Public API
// ----------------------------------

struct c64_network_buffer *c64_network_buffer_create(void)
{
    struct c64_network_buffer *buf = (struct c64_network_buffer *)malloc(sizeof(struct c64_network_buffer));
    if (!buf) {
        C64_LOG_ERROR("Failed to allocate network buffer");
        return NULL;
    }

    // Initialize video buffer
    buf->video.slots = buf->video_slots;
    buf->video.max_capacity = C64_MAX_VIDEO_PACKETS;
    buf->video.packet_size = C64_VIDEO_PACKET_SIZE;
    buf->video.delay_us = 0; // Initialize with no delay by default
    buf->video.type = BUFFER_TYPE_VIDEO;
    pthread_mutex_init(&buf->video.mutex, NULL);
    rb_reset(&buf->video, C64_MAX_VIDEO_PACKETS);

    // Initialize audio buffer (use smaller allocation for audio)
    buf->audio.slots = buf->audio_slots;
    buf->audio.max_capacity = C64_MAX_AUDIO_PACKETS;
    buf->audio.packet_size = C64_AUDIO_PACKET_SIZE;
    buf->audio.delay_us = 0; // Initialize with no delay by default
    buf->audio.type = BUFFER_TYPE_AUDIO;
    pthread_mutex_init(&buf->audio.mutex, NULL);
    rb_reset(&buf->audio, C64_MAX_AUDIO_PACKETS);

    C64_LOG_INFO("Network buffer created - Video: %zu slots, Audio: %zu slots", (size_t)C64_MAX_VIDEO_PACKETS,
                 (size_t)C64_MAX_AUDIO_PACKETS);

    return buf;
}

void c64_network_buffer_destroy(struct c64_network_buffer *buf)
{
    if (!buf) {
        return;
    }

    C64_LOG_INFO("Network buffer destroyed");
    pthread_mutex_destroy(&buf->video.mutex);
    pthread_mutex_destroy(&buf->audio.mutex);
    free(buf);
}

void c64_network_buffer_set_delay(struct c64_network_buffer *buf, size_t video_delay_ms, size_t audio_delay_ms)
{
    if (!buf) {
        return;
    }

    // Clamp delay values to maximum
    if (video_delay_ms > C64_MAX_DELAY_MS) {
        video_delay_ms = C64_MAX_DELAY_MS;
    }
    if (audio_delay_ms > C64_MAX_DELAY_MS) {
        audio_delay_ms = C64_MAX_DELAY_MS;
    }

    // Compute active slots based on packet rates
    size_t video_slots = (size_t)ceil((C64_MAX_VIDEO_RATE * video_delay_ms) / 1000.0);
    if (video_slots > buf->video.max_capacity) {
        video_slots = buf->video.max_capacity;
    }

    size_t audio_slots = (size_t)ceil((C64_MAX_AUDIO_RATE * audio_delay_ms) / 1000.0);
    if (audio_slots > buf->audio.max_capacity) {
        audio_slots = buf->audio.max_capacity;
    }

    // Store new delay values in microseconds
    uint64_t old_video_delay = buf->video.delay_us;
    uint64_t old_audio_delay = buf->audio.delay_us;
    buf->video.delay_us = video_delay_ms * 1000;
    buf->audio.delay_us = audio_delay_ms * 1000;

    C64_LOG_INFO("Buffer delay values set: video=%llu us (%zu ms), audio=%llu us (%zu ms)",
                 (unsigned long long)buf->video.delay_us, video_delay_ms, (unsigned long long)buf->audio.delay_us,
                 audio_delay_ms);

    // If delay was reduced, discard excess packets to reach new buffer size
    // IMPORTANT: Discard from tail (oldest in sequence order) to maintain packet ordering

    if (buf->video.delay_us < old_video_delay) {
        // Calculate how many packets to keep for new delay
        size_t new_video_capacity = video_slots;
        pthread_mutex_lock(&buf->video.mutex);
        size_t head = buf->video.head;
        size_t tail = buf->video.tail;

        // Count current packets in buffer
        size_t current_packets = (head >= tail) ? (head - tail) : (buf->video.max_capacity - tail + head);

        if (current_packets > new_video_capacity) {
            size_t packets_to_discard = current_packets - new_video_capacity;
            size_t discarded = 0;

            // Discard from tail (oldest packets) to maintain sequence order
            while (discarded < packets_to_discard && tail != head) {
                struct packet_slot *slot = &buf->video.slots[tail];
                if (slot->valid) {
                    slot->valid = false;
                    discarded++;
                }
                buf->video.tail = (tail + 1) % buf->video.max_capacity;
                tail = buf->video.tail;
            }

            if (discarded > 0) {
                C64_LOG_INFO("Video buffer: discarded %zu old packets due to delay reduction (sequence-ordered)",
                             discarded);
            }
        }
        pthread_mutex_unlock(&buf->video.mutex);
    }

    if (buf->audio.delay_us < old_audio_delay) {
        // Calculate how many packets to keep for new delay
        size_t new_audio_capacity = audio_slots;
        pthread_mutex_lock(&buf->audio.mutex);
        size_t head = buf->audio.head;
        size_t tail = buf->audio.tail;

        // Count current packets in buffer
        size_t current_packets = (head >= tail) ? (head - tail) : (buf->audio.max_capacity - tail + head);

        if (current_packets > new_audio_capacity) {
            size_t packets_to_discard = current_packets - new_audio_capacity;
            size_t discarded = 0;

            // Discard from tail (oldest packets) to maintain sequence order
            while (discarded < packets_to_discard && tail != head) {
                struct packet_slot *slot = &buf->audio.slots[tail];
                if (slot->valid) {
                    slot->valid = false;
                    discarded++;
                }
                buf->audio.tail = (tail + 1) % buf->audio.max_capacity;
                tail = buf->audio.tail;
            }

            if (discarded > 0) {
                C64_LOG_INFO("Audio buffer: discarded %zu old packets due to delay reduction (sequence-ordered)",
                             discarded);
            }
        }
        pthread_mutex_unlock(&buf->audio.mutex);
    }

    C64_LOG_INFO("Network buffer delay set - Video: %zu ms (%zu slots), Audio: %zu ms (%zu slots)", video_delay_ms,
                 video_slots, audio_delay_ms, audio_slots);
}

void c64_network_buffer_push_video(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns)
{
    if (!buf || !data) {
        return;
    }

    static int push_count = 0;
    if ((push_count++ % 5000) == 0) {
        printf("[DEBUG] Network buffer push video: packet %d (len=%zu)\n", push_count, len);
    }

    // Convert nanoseconds to microseconds for internal storage
    uint64_t timestamp_us = timestamp_ns / 1000;
    rb_push(&buf->video, data, len, timestamp_us);
}

void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns)
{
    if (!buf || !data) {
        return;
    }

    // Convert nanoseconds to microseconds for internal storage
    uint64_t timestamp_us = timestamp_ns / 1000;
    rb_push(&buf->audio, data, len, timestamp_us);
}

// Check if oldest packet in buffer has been delayed long enough
static bool is_packet_ready_for_pop(struct packet_slot *slot, uint64_t delay_us)
{
    if (!slot->valid) {
        return false;
    }

    // Get current time in microseconds (OBS uses nanoseconds, so convert)
    uint64_t now_us = os_gettime_ns() / 1000;
    uint64_t age_us = now_us - slot->timestamp_us;
    bool ready = age_us >= delay_us;

    // Debug logging for delay timing - log every 1000 checks to reduce spam
    static int delay_debug_count = 0;
    if ((delay_debug_count++ % 1000) == 0) {
        C64_LOG_DEBUG("🕰️ DELAY CHECK #%d: age=%llu us, required=%llu us, ready=%s (seq=%u)", delay_debug_count,
                      (unsigned long long)age_us, (unsigned long long)delay_us, ready ? "YES" : "NO",
                      slot->sequence_num);
    }

    return ready;
}

int c64_network_buffer_pop(struct c64_network_buffer *buf, const uint8_t **video_data, size_t *video_size,
                           const uint8_t **audio_data, size_t *audio_size, uint64_t *timestamp_us)
{
    if (!buf || !video_data || !audio_data) {
        return 0;
    }

    // Check if we have packets available
    pthread_mutex_lock(&buf->video.mutex);
    size_t video_head = buf->video.head;
    size_t video_tail = buf->video.tail;
    pthread_mutex_unlock(&buf->video.mutex);

    if (video_head == video_tail) {
        // No video packets available
        static int no_video_count = 0;
        if ((no_video_count++ % 100) == 0) {
            C64_LOG_INFO("📦 BUFFER EMPTY: No video packets available (attempt #%d)", no_video_count);
        }
        return 0;
    }

    // Check if the OLDEST packet (at tail) has been delayed long enough
    struct packet_slot *oldest_video = &buf->video.slots[video_tail];
    if (!oldest_video->valid || !is_packet_ready_for_pop(oldest_video, buf->video.delay_us)) {
        // Oldest packet not ready yet - must wait to preserve FIFO ordering
        static int delay_count = 0;
        if ((delay_count++ % 100) == 0) {
            uint64_t now_us = os_gettime_ns() / 1000;
            uint64_t age_us = oldest_video->valid ? (now_us - oldest_video->timestamp_us) : 0;
            C64_LOG_INFO("⏰ DELAY WAIT: Oldest packet age=%llu us, need=%llu us (attempt #%d)",
                         (unsigned long long)age_us, (unsigned long long)buf->video.delay_us, delay_count);
        }
        return 0;
    }

    // Pop the ready video packet (oldest first for proper FIFO)
    struct packet_slot *v;
    if (!rb_pop_oldest(&buf->video, &v)) {
        C64_LOG_WARNING("Failed to pop ready video packet");
        return 0;
    }

    // Try to get audio packet (optional - and check its delay too)
    struct packet_slot *a = NULL;
    bool has_audio = false;

    pthread_mutex_lock(&buf->audio.mutex);
    size_t audio_head = buf->audio.head;
    size_t audio_tail = buf->audio.tail;
    pthread_mutex_unlock(&buf->audio.mutex);

    if (audio_head != audio_tail) {
        struct packet_slot *oldest_audio = &buf->audio.slots[audio_tail];
        if (is_packet_ready_for_pop(oldest_audio, buf->audio.delay_us)) {
            has_audio = rb_pop_oldest(&buf->audio, &a);
        }
    }

    static int pop_count = 0;
    if ((pop_count++ % 1000) == 0) {
        C64_LOG_DEBUG("Network buffer pop SUCCESS: video=yes, audio=%s (count: %d)", has_audio ? "yes" : "no",
                      pop_count);
    }

    // Return packet data
    *video_data = v->data;
    *video_size = v->size;

    if (has_audio && a) {
        *audio_data = a->data;
        *audio_size = a->size;
    } else {
        *audio_data = NULL;
        *audio_size = 0;
    }

    // Return timestamp (use video timestamp if no audio)
    if (timestamp_us) {
        *timestamp_us = (has_audio && a && a->timestamp_us < v->timestamp_us) ? a->timestamp_us : v->timestamp_us;
    }

    return 1;
}

void c64_network_buffer_flush(struct c64_network_buffer *buf)
{
    if (!buf) {
        return;
    }

    // Reset both buffers to empty state while maintaining active slot counts
    size_t video_active = buf->video.active_slots;
    size_t audio_active = buf->audio.active_slots;

    rb_reset(&buf->video, video_active);
    rb_reset(&buf->audio, audio_active);

    C64_LOG_INFO("Network buffers flushed");
}
