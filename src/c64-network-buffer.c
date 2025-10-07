#include "c64-network-buffer.h"
#include "c64-logging.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

struct packet_slot {
    uint8_t data[C64_VIDEO_PACKET_SIZE > C64_AUDIO_PACKET_SIZE ? C64_VIDEO_PACKET_SIZE : C64_AUDIO_PACKET_SIZE];
    size_t size;
    uint64_t timestamp_us;
};

struct video_ring_buffer {
    struct packet_slot slots[C64_MAX_VIDEO_PACKETS]; // Full video buffer allocation
    size_t max_capacity;                             // C64_MAX_VIDEO_PACKETS
    size_t active_slots;                             // computed from delay
    atomic_size_t head;
    atomic_size_t tail;
    size_t packet_size; // C64_VIDEO_PACKET_SIZE
};

struct audio_ring_buffer {
    struct packet_slot slots[C64_MAX_AUDIO_PACKETS]; // Smaller audio buffer allocation
    size_t max_capacity;                             // C64_MAX_AUDIO_PACKETS
    size_t active_slots;                             // computed from delay
    atomic_size_t head;
    atomic_size_t tail;
    size_t packet_size; // C64_AUDIO_PACKET_SIZE
};

struct c64_network_buffer {
    struct video_ring_buffer video;
    struct audio_ring_buffer audio;
};

// ----------------------------------
// Internal helpers
// ----------------------------------

// Generic ring buffer operations using macros to work with both buffer types
#define RB_INCREMENT(rb) (((rb)->head + 1) % (rb)->max_capacity)
#define RB_RESET(rb, active) do { \
    atomic_store(&(rb)->head, 0); \
    atomic_store(&(rb)->tail, 0); \
    (rb)->active_slots = (active); \
} while(0)

// Push packet into video ring buffer. Drops oldest if full.
static void video_rb_push(struct video_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    if (!rb || !data || len == 0) {
        return;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    size_t next = (head + 1) % rb->max_capacity;

    if (next == tail) {
        // Buffer full: drop oldest packet
        atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
    }

    size_t copy_len = len < rb->packet_size ? len : rb->packet_size;
    memcpy(rb->slots[head].data, data, copy_len);

    // Zero-pad if packet is smaller than expected size
    if (copy_len < rb->packet_size) {
        memset(rb->slots[head].data + copy_len, 0, rb->packet_size - copy_len);
    }

    rb->slots[head].size = rb->packet_size;
    rb->slots[head].timestamp_us = ts;
    atomic_store_explicit(&rb->head, next, memory_order_release);
}

// Push packet into audio ring buffer. Drops oldest if full.
static void audio_rb_push(struct audio_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    if (!rb || !data || len == 0) {
        return;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    size_t next = (head + 1) % rb->max_capacity;

    if (next == tail) {
        // Buffer full: drop oldest packet
        atomic_store_explicit(&rb->tail, (tail + 1) % rb->max_capacity, memory_order_release);
    }

    size_t copy_len = len < rb->packet_size ? len : rb->packet_size;
    memcpy(rb->slots[head].data, data, copy_len);

    // Zero-pad if packet is smaller than expected size
    if (copy_len < rb->packet_size) {
        memset(rb->slots[head].data + copy_len, 0, rb->packet_size - copy_len);
    }

    rb->slots[head].size = rb->packet_size;
    rb->slots[head].timestamp_us = ts;
    atomic_store_explicit(&rb->head, next, memory_order_release);
}

// Pop the latest video packet within active slots
static int video_rb_pop_latest(struct video_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    size_t count = (head >= tail) ? (head - tail) : (rb->max_capacity - tail + head);

    if (count == 0) {
        // No packets available
        return 0;
    }

    size_t last = (head == 0) ? rb->max_capacity - 1 : head - 1;
    *out = &rb->slots[last];
    atomic_store_explicit(&rb->tail, head, memory_order_release);
    return 1;
}

// Pop the latest audio packet within active slots
static int audio_rb_pop_latest(struct audio_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    size_t count = (head >= tail) ? (head - tail) : (rb->max_capacity - tail + head);

    if (count == 0) {
        // No packets available
        return 0;
    }

    size_t last = (head == 0) ? rb->max_capacity - 1 : head - 1;
    *out = &rb->slots[last];
    atomic_store_explicit(&rb->tail, head, memory_order_release);
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
    video_rb_reset(&buf->video, C64_MAX_VIDEO_PACKETS);

    // Initialize audio buffer (use smaller allocation for audio)
    buf->audio.max_capacity = C64_MAX_AUDIO_PACKETS;
    buf->audio.packet_size = C64_AUDIO_PACKET_SIZE;
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

    // Reset buffers with new active slot counts
    video_rb_reset(&buf->video, video_slots);
    audio_rb_reset(&buf->audio, audio_slots);

    C64_LOG_INFO("Network buffer delay set - Video: %zu ms (%zu slots), Audio: %zu ms (%zu slots)", video_delay_ms,
                 video_slots, audio_delay_ms, audio_slots);
}

void c64_network_buffer_push_video(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_us)
{
    if (!buf || !data) {
        return;
    }

    static int push_count = 0;
    if ((push_count++ % 1000) == 0) {
        printf("[DEBUG] Network buffer push video: packet %d (len=%zu)\n", push_count, len);
    }

    video_rb_push(&buf->video, data, len, timestamp_us);
}

void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_us)
{
    if (!buf || !data) {
        return;
    }
    audio_rb_push(&buf->audio, data, len, timestamp_us);
}

int c64_network_buffer_pop(struct c64_network_buffer *buf, const uint8_t **video_data, size_t *video_size,
                           const uint8_t **audio_data, size_t *audio_size, uint64_t *timestamp_us)
{
    if (!buf || !video_data || !audio_data) {
        return 0;
    }

    struct packet_slot *v, *a;

    // Try to get video packet (required)
    if (!video_rb_pop_latest(&buf->video, &v)) {
        static int no_video_count = 0;
        if ((no_video_count++ % 1000) == 0) {
            printf("[DEBUG] Network buffer pop: no video packets available (count: %d)\n", no_video_count);
        }
        return 0;
    }

    // Try to get audio packet (optional - may not always be available)
    bool has_audio = audio_rb_pop_latest(&buf->audio, &a);

    static int pop_count = 0;
    if ((pop_count++ % 100) == 0) {
        printf("[DEBUG] Network buffer pop SUCCESS: video=yes, audio=%s (count: %d)\n", has_audio ? "yes" : "no",
               pop_count);
    }

    // Return packet data
    *video_data = v->data;
    *video_size = v->size;

    if (has_audio) {
        *audio_data = a->data;
        *audio_size = a->size;
    } else {
        *audio_data = NULL;
        *audio_size = 0;
    }

    // Return timestamp (use video timestamp if no audio)
    if (timestamp_us) {
        *timestamp_us = has_audio && a->timestamp_us < v->timestamp_us ? a->timestamp_us : v->timestamp_us;
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
