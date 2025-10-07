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

struct video_ring_buffer {
    struct packet_slot slots[C64_MAX_VIDEO_PACKETS]; // Full video buffer allocation
    size_t max_capacity;                             // C64_MAX_VIDEO_PACKETS
    size_t active_slots;                             // computed from delay
    atomic_size_t head;
    atomic_size_t tail;
    size_t packet_size;         // C64_VIDEO_PACKET_SIZE
    uint16_t next_expected_seq; // Expected next sequence number
    bool seq_initialized;       // Whether we have initialized sequence tracking
    uint64_t delay_us;          // Delay in microseconds
};

struct audio_ring_buffer {
    struct packet_slot slots[C64_MAX_AUDIO_PACKETS]; // Smaller audio buffer allocation
    size_t max_capacity;                             // C64_MAX_AUDIO_PACKETS
    size_t active_slots;                             // computed from delay
    atomic_size_t head;
    atomic_size_t tail;
    size_t packet_size;         // C64_AUDIO_PACKET_SIZE
    uint16_t next_expected_seq; // Expected next sequence number
    bool seq_initialized;       // Whether we have initialized sequence tracking
    uint64_t delay_us;          // Delay in microseconds
};

struct c64_network_buffer {
    struct video_ring_buffer video;
    struct audio_ring_buffer audio;
};

// ----------------------------------
// Internal helpers
// ----------------------------------

// Debug function to verify buffer sequence ordering
static void debug_verify_buffer_ordering(struct video_ring_buffer *rb, const char *context)
{
#ifdef DEBUG_SEQUENCE_ORDERING
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

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
#define RB_INCREMENT(rb) (((rb)->head + 1) % (rb)->max_capacity)
#define RB_RESET(rb, active) do { \
    atomic_store(&(rb)->head, 0); \
    atomic_store(&(rb)->tail, 0); \
    (rb)->active_slots = (active); \
} while(0)

// Push packet into video ring buffer with sequence-based ordering
static void video_rb_push(struct video_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    if (!rb || !data || len < 2) { // Need at least 2 bytes for sequence number
        return;
    }

    // Extract sequence number from packet header (first 2 bytes, little-endian)
    uint16_t seq_num = *(uint16_t *)(data);

    // Initialize sequence tracking on first packet
    if (!rb->seq_initialized) {
        rb->next_expected_seq = seq_num;
        rb->seq_initialized = true;
        C64_LOG_DEBUG("Video buffer: initialized with sequence %u", seq_num);
    }

    // Check if this is the next expected packet in sequence
    bool is_next_expected = (seq_num == rb->next_expected_seq);

    if (!is_next_expected) {
        // Handle out-of-order packet: check if it's from the future or past
        int16_t seq_diff = (int16_t)(seq_num - rb->next_expected_seq);
        if (seq_diff > 0 && seq_diff < 100) {
            // Future packet - this indicates missing packets
            C64_LOG_WARNING("Video: Out-of-order packet seq=%u (expected %u, gap=%d)", seq_num, rb->next_expected_seq,
                            seq_diff);
        } else if (seq_diff < 0 && seq_diff > -100) {
            // Old packet - likely duplicate, drop it
            C64_LOG_DEBUG("Video: Dropping old packet seq=%u (expected %u)", seq_num, rb->next_expected_seq);
            return;
        } else {
            // Large sequence jump - likely a reset, re-initialize
            C64_LOG_INFO("Video: Large sequence jump from %u to %u, re-initializing", rb->next_expected_seq, seq_num);
            rb->next_expected_seq = seq_num;
        }
    }

    // Find insertion point to maintain sequence order
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    // Check if buffer is full
    size_t next_head = (head + 1) % rb->max_capacity;
    if (next_head == tail) {
        // Buffer full: drop oldest packet
        atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
        tail = (tail + 1) % rb->max_capacity;
    }

    // Find correct insertion position based on sequence number
    size_t insert_pos = head;
    size_t current = head;

    // Walk backwards from head to find insertion point
    while (current != tail) {
        size_t prev = (current == 0) ? rb->max_capacity - 1 : current - 1;
        if (prev == tail)
            break; // Reached tail, insert at current position

        if (!rb->slots[prev].valid) {
            current = prev;
            continue;
        }

        // Compare sequence numbers with wraparound handling
        int16_t seq_diff = (int16_t)(seq_num - rb->slots[prev].sequence_num);
        if (seq_diff >= 0) {
            // Current packet should go after this position
            break;
        }

        current = prev;
        insert_pos = current;
    }

    // Shift packets forward if we're not inserting at head
    if (insert_pos != head) {
        size_t shift_pos = head;
        while (shift_pos != insert_pos) {
            size_t prev = (shift_pos == 0) ? rb->max_capacity - 1 : shift_pos - 1;
            rb->slots[shift_pos] = rb->slots[prev];
            shift_pos = prev;
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

    // Update expected sequence number if this was the expected packet
    if (is_next_expected) {
        rb->next_expected_seq = seq_num + 1;
    }

    // Log sequence insertion details for debugging
    if (insert_pos != head) {
        C64_LOG_DEBUG("Video: Inserted seq %u at pos %zu (head was %zu)", seq_num, insert_pos, head);
    }

    atomic_store_explicit(&rb->head, next_head, memory_order_release);

    // Verify buffer ordering in debug builds
    debug_verify_buffer_ordering(rb, "video_rb_push");
}

// Push packet into audio ring buffer with sequence-based ordering
static void audio_rb_push(struct audio_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    if (!rb || !data || len < 2) { // Need at least 2 bytes for sequence number
        return;
    }

    // Extract sequence number from packet header (first 2 bytes, little-endian)
    uint16_t seq_num = *(uint16_t *)(data);

    // Initialize sequence tracking on first packet
    if (!rb->seq_initialized) {
        rb->next_expected_seq = seq_num;
        rb->seq_initialized = true;
        C64_LOG_DEBUG("Audio buffer: initialized with sequence %u", seq_num);
    }

    // Check if this is the next expected packet in sequence
    bool is_next_expected = (seq_num == rb->next_expected_seq);

    if (!is_next_expected) {
        // Handle out-of-order packet
        int16_t seq_diff = (int16_t)(seq_num - rb->next_expected_seq);
        if (seq_diff > 0 && seq_diff < 50) {
            // Future packet - this indicates missing packets
            C64_LOG_WARNING("Audio: Out-of-order packet seq=%u (expected %u, gap=%d)", seq_num, rb->next_expected_seq,
                            seq_diff);
        } else if (seq_diff < 0 && seq_diff > -50) {
            // Old packet - likely duplicate, drop it
            C64_LOG_DEBUG("Audio: Dropping old packet seq=%u (expected %u)", seq_num, rb->next_expected_seq);
            return;
        } else {
            // Large sequence jump - likely a reset, re-initialize
            C64_LOG_INFO("Audio: Large sequence jump from %u to %u, re-initializing", rb->next_expected_seq, seq_num);
            rb->next_expected_seq = seq_num;
        }
    }

    // Find insertion point to maintain sequence order
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    // Check if buffer is full
    size_t next_head = (head + 1) % rb->max_capacity;
    if (next_head == tail) {
        // Buffer full: drop oldest packet
        atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
        tail = (tail + 1) % rb->max_capacity;
    }

    // Find correct insertion position based on sequence number
    size_t insert_pos = head;
    size_t current = head;

    // Walk backwards from head to find insertion point
    while (current != tail) {
        size_t prev = (current == 0) ? rb->max_capacity - 1 : current - 1;
        if (prev == tail)
            break; // Reached tail, insert at current position

        if (!rb->slots[prev].valid) {
            current = prev;
            continue;
        }

        // Compare sequence numbers with wraparound handling
        int16_t seq_diff = (int16_t)(seq_num - rb->slots[prev].sequence_num);
        if (seq_diff >= 0) {
            // Current packet should go after this position
            break;
        }

        current = prev;
        insert_pos = current;
    }

    // Shift packets forward if we're not inserting at head
    if (insert_pos != head) {
        size_t shift_pos = head;
        while (shift_pos != insert_pos) {
            size_t prev = (shift_pos == 0) ? rb->max_capacity - 1 : shift_pos - 1;
            rb->slots[shift_pos] = rb->slots[prev];
            shift_pos = prev;
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

    // Update expected sequence number if this was the expected packet
    if (is_next_expected) {
        rb->next_expected_seq = seq_num + 1;
    }

    // Log sequence insertion details for debugging
    if (insert_pos != head) {
        C64_LOG_DEBUG("Audio: Inserted seq %u at pos %zu (head was %zu)", seq_num, insert_pos, head);
    }

    atomic_store_explicit(&rb->head, next_head, memory_order_release);
}

// Pop the oldest video packet (FIFO order) - essential for proper frame assembly
static int video_rb_pop_oldest(struct video_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (head == tail) {
        // No packets available
        return 0;
    }

    *out = &rb->slots[tail];
    atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
    return 1;
}

// Pop the oldest audio packet (FIFO order)
static int audio_rb_pop_oldest(struct audio_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (head == tail) {
        // No packets available
        return 0;
    }

    *out = &rb->slots[tail];
    atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
    return 1;
}

// Reset video buffer with new active slot count
static void video_rb_reset(struct video_ring_buffer *rb, size_t active_slots)
{
    if (!rb) {
        return;
    }

    if (active_slots > rb->max_capacity) {
        active_slots = rb->max_capacity;
    }

    rb->active_slots = active_slots;
    atomic_store_explicit(&rb->head, 0, memory_order_release);
    atomic_store_explicit(&rb->tail, 0, memory_order_release);

    // Reset sequence tracking
    rb->next_expected_seq = 0;
    rb->seq_initialized = false;

    // Clear all slot validity
    for (size_t i = 0; i < rb->max_capacity; i++) {
        rb->slots[i].valid = false;
    }
}

// Reset audio buffer with new active slot count
static void audio_rb_reset(struct audio_ring_buffer *rb, size_t active_slots)
{
    if (!rb) {
        return;
    }

    if (active_slots > rb->max_capacity) {
        active_slots = rb->max_capacity;
    }

    rb->active_slots = active_slots;
    atomic_store_explicit(&rb->head, 0, memory_order_release);
    atomic_store_explicit(&rb->tail, 0, memory_order_release);

    // Reset sequence tracking
    rb->next_expected_seq = 0;
    rb->seq_initialized = false;

    // Clear all slot validity
    for (size_t i = 0; i < rb->max_capacity; i++) {
        rb->slots[i].valid = false;
    }
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
    buf->video.max_capacity = C64_MAX_VIDEO_PACKETS;
    buf->video.packet_size = C64_VIDEO_PACKET_SIZE;
    buf->video.delay_us = 0; // Initialize with no delay by default
    video_rb_reset(&buf->video, C64_MAX_VIDEO_PACKETS);

    // Initialize audio buffer (use smaller allocation for audio)
    buf->audio.max_capacity = C64_MAX_AUDIO_PACKETS;
    buf->audio.packet_size = C64_AUDIO_PACKET_SIZE;
    buf->audio.delay_us = 0; // Initialize with no delay by default
    audio_rb_reset(&buf->audio, C64_MAX_AUDIO_PACKETS);

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

    // If delay was reduced, discard packets older than new delay
    uint64_t current_time = os_gettime_ns() / 1000; // Convert to microseconds

    if (buf->video.delay_us < old_video_delay) {
        // Discard old video packets
        size_t head = atomic_load_explicit(&buf->video.head, memory_order_acquire);
        size_t tail = atomic_load_explicit(&buf->video.tail, memory_order_acquire);
        size_t discarded = 0;

        while (tail != head) {
            struct packet_slot *slot = &buf->video.slots[tail];
            if (slot->valid && (current_time - slot->timestamp_us) > buf->video.delay_us) {
                slot->valid = false;
                atomic_store_explicit(&buf->video.tail, (tail + 1) % buf->video.max_capacity, memory_order_release);
                tail = (tail + 1) % buf->video.max_capacity;
                discarded++;
            } else {
                break; // First packet within new delay, stop discarding
            }
        }

        if (discarded > 0) {
            C64_LOG_INFO("Video buffer: discarded %zu old packets due to delay reduction", discarded);
        }
    }

    if (buf->audio.delay_us < old_audio_delay) {
        // Discard old audio packets
        size_t head = atomic_load_explicit(&buf->audio.head, memory_order_acquire);
        size_t tail = atomic_load_explicit(&buf->audio.tail, memory_order_acquire);
        size_t discarded = 0;

        while (tail != head) {
            struct packet_slot *slot = &buf->audio.slots[tail];
            if (slot->valid && (current_time - slot->timestamp_us) > buf->audio.delay_us) {
                slot->valid = false;
                atomic_store_explicit(&buf->audio.tail, (tail + 1) % buf->audio.max_capacity, memory_order_release);
                tail = (tail + 1) % buf->audio.max_capacity;
                discarded++;
            } else {
                break; // First packet within new delay, stop discarding
            }
        }

        if (discarded > 0) {
            C64_LOG_INFO("Audio buffer: discarded %zu old packets due to delay reduction", discarded);
        }
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
    video_rb_push(&buf->video, data, len, timestamp_us);
}

void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns)
{
    if (!buf || !data) {
        return;
    }

    // Convert nanoseconds to microseconds for internal storage
    uint64_t timestamp_us = timestamp_ns / 1000;
    audio_rb_push(&buf->audio, data, len, timestamp_us);
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

    // Debug logging for delay timing
    static int delay_debug_count = 0;
    if ((delay_debug_count++ % 1000) == 0) {
        C64_LOG_DEBUG("Packet delay check: age=%llu us, required=%llu us, ready=%s (seq=%u)",
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

    // Check if we have packets available and if they've been delayed long enough
    size_t video_head = atomic_load_explicit(&buf->video.head, memory_order_acquire);
    size_t video_tail = atomic_load_explicit(&buf->video.tail, memory_order_acquire);

    if (video_head == video_tail) {
        // No video packets available
        static int no_video_count = 0;
        if ((no_video_count++ % 1000) == 0) {
            C64_LOG_DEBUG("Network buffer pop: no video packets available (count: %d)", no_video_count);
        }
        return 0;
    }

    // Check if oldest video packet has been delayed long enough
    struct packet_slot *oldest_video = &buf->video.slots[video_tail];
    if (!is_packet_ready_for_pop(oldest_video, buf->video.delay_us)) {
        // Packet not ready yet - need more delay
        static int delay_count = 0;
        if ((delay_count++ % 500) == 0) {
            C64_LOG_DEBUG("Network buffer pop: video packet not ready yet (delay active, count: %d)", delay_count);
        }
        return 0;
    }

    // Pop the video packet (it's ready)
    struct packet_slot *v;
    if (!video_rb_pop_oldest(&buf->video, &v)) {
        C64_LOG_WARNING("Failed to pop video packet that was just checked as available");
        return 0;
    }

    // Try to get audio packet (optional - and check its delay too)
    struct packet_slot *a = NULL;
    bool has_audio = false;

    size_t audio_head = atomic_load_explicit(&buf->audio.head, memory_order_acquire);
    size_t audio_tail = atomic_load_explicit(&buf->audio.tail, memory_order_acquire);

    if (audio_head != audio_tail) {
        struct packet_slot *oldest_audio = &buf->audio.slots[audio_tail];
        if (is_packet_ready_for_pop(oldest_audio, buf->audio.delay_us)) {
            has_audio = audio_rb_pop_oldest(&buf->audio, &a);
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

    video_rb_reset(&buf->video, video_active);
    audio_rb_reset(&buf->audio, audio_active);

    C64_LOG_INFO("Network buffers flushed");
}
