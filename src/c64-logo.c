/**
 * @file c64-logo.c
 * @brief C64 Ultimate logo rendering for async video output
 *
 * This module handles loading and pre-rendering the C64 Ultimate logo
 * for display during no-connection states. The logo is pre-rendered
 * during plugin initialization to eliminate any delays during runtime.
 */

#include <inttypes.h>
#include <string.h>
#include <obs-module.h>
#include <util/platform.h>
#include "c64-logo.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-color.h"

// Load the C64S logo texture from module data directory
static gs_texture_t *load_logo_texture(void)
{
    C64_LOG_DEBUG("Attempting to load logo texture...");

    // Use obs_module_file() to get the path to the logo in the data directory
    char *logo_path = obs_module_file("images/c64stream-logo.png");
    if (!logo_path) {
        C64_LOG_WARNING("Failed to locate logo file in module data directory");
        return NULL;
    }

    C64_LOG_DEBUG("Logo path resolved to: %s", logo_path);

    // Load texture from file
    gs_texture_t *logo_texture = gs_texture_create_from_file(logo_path);

    if (!logo_texture) {
        C64_LOG_WARNING("Failed to load logo texture from: %s", logo_path);
    } else {
        uint32_t width = gs_texture_get_width(logo_texture);
        uint32_t height = gs_texture_get_height(logo_texture);
        C64_LOG_DEBUG("Loaded logo texture from: %s (size: %ux%u)", logo_path, width, height);
    }

    bfree(logo_path);
    return logo_texture;
}

// Pre-render the logo into a frame buffer for instant async video output
static bool prerender_logo_frame(struct c64_source *context)
{
    if (!context->logo_frame_buffer) {
        return false;
    }

    uint32_t *buffer = context->logo_frame_buffer;
    uint32_t width = context->width;
    uint32_t height = context->height;

    // C64 authentic colors from VIC-II palette
    const uint32_t c64_border_color = vic_colors[6]; // Dark Blue border (authentic C64 style)
    const uint32_t c64_screen_color = vic_colors[0]; // Black screen area

    // Create authentic C64 display layout
    // Fill entire frame with dark blue border color first
    for (uint32_t i = 0; i < width * height; i++) {
        buffer[i] = c64_border_color;
    }

    // Precise C64 border dimensions from bootscreen analysis
    uint32_t border_left, border_right, border_top, border_bottom;

    if (height == C64_NTSC_HEIGHT) {
        // NTSC (384×240): L32|R32|T20|B20 → 320×200 screen
        border_left = 32;
        border_right = 32;
        border_top = 20;
        border_bottom = 20;
    } else {
        // PAL (384×272): L32|R32|T35|B37 → 320×200 screen (assume PAL if unknown)
        border_left = 32;
        border_right = 32;
        border_top = 35;
        border_bottom = 37;
    }

    uint32_t screen_x = border_left;
    uint32_t screen_y = border_top;
    uint32_t screen_width = width - border_left - border_right;
    uint32_t screen_height = height - border_top - border_bottom;

    // Fill screen area with dark blue
    for (uint32_t y = screen_y; y < screen_y + screen_height && y < height; y++) {
        for (uint32_t x = screen_x; x < screen_x + screen_width && x < width; x++) {
            buffer[y * width + x] = c64_screen_color;
        }
    }

    // If logo texture is available, render it scaled to 70% of screen width
    if (context->logo_texture) {
        uint32_t original_logo_width = gs_texture_get_width(context->logo_texture);
        uint32_t original_logo_height = gs_texture_get_height(context->logo_texture);

        // Scale logo to 70% of the black screen area width (320 pixels = screen_width)
        uint32_t target_logo_width = (screen_width * 70) / 100; // 70% of 320 = 224 pixels
        uint32_t target_logo_height =
            (original_logo_height * target_logo_width) / original_logo_width; // Maintain aspect ratio

        // Calculate logo position (centered in black screen area)
        uint32_t logo_x = screen_x + (screen_width - target_logo_width) / 2;
        uint32_t logo_y = screen_y + (screen_height - target_logo_height) / 2;

        // Use target dimensions for rendering
        uint32_t logo_w = target_logo_width;
        uint32_t logo_h = target_logo_height;

        // Since we can't directly read texture pixels without graphics context,
        // create a subtle logo placeholder that indicates where the logo will be
        const uint32_t logo_placeholder = 0xFF404040; // Subtle gray

        for (uint32_t y = logo_y; y < logo_y + logo_h && y < height; y++) {
            for (uint32_t x = logo_x; x < logo_x + logo_w && x < width; x++) {
                // Create a simple centered rectangle as logo placeholder
                buffer[y * width + x] = logo_placeholder;
            }
        }

        C64_LOG_INFO(
            "🔷 Pre-rendered C64 display with logo placeholder: %ux%u (70%% of screen) at (%u,%u) in %ux%u frame",
            logo_w, logo_h, logo_x, logo_y, width, height);
    } else {
        C64_LOG_INFO("🔷 Pre-rendered authentic C64 display: %ux%u frame with borders", width, height);
    }
    return true;
}

// Initialize logo system - load texture and pre-render frame buffer
bool c64_logo_init(struct c64_source *context)
{
    if (!context) {
        return false;
    }

    C64_LOG_DEBUG("Initializing logo system...");

    // Allocate pre-rendered logo frame buffer (same format as main frame buffer)
    size_t frame_size = context->width * context->height * sizeof(uint32_t);
    context->logo_frame_buffer = bmalloc(frame_size);
    if (!context->logo_frame_buffer) {
        C64_LOG_ERROR("Failed to allocate logo frame buffer (%zu bytes)", frame_size);
        return false;
    }

    // Load logo texture immediately during initialization
    context->logo_texture = load_logo_texture();
    context->logo_texture_loaded = true;

    // Pre-render the logo frame for instant display
    if (!prerender_logo_frame(context)) {
        C64_LOG_WARNING("Failed to pre-render logo frame during initialization");
    }

    C64_LOG_INFO("✅ Logo system initialized successfully (%zu bytes, texture %s)", frame_size,
                 context->logo_texture ? "loaded" : "fallback");
    return true;
}

// Cleanup logo resources
void c64_logo_cleanup(struct c64_source *context)
{
    if (!context) {
        return;
    }

    C64_LOG_DEBUG("Cleaning up logo system...");

    if (context->logo_texture) {
        gs_texture_destroy(context->logo_texture);
        context->logo_texture = NULL;
    }

    if (context->logo_frame_buffer) {
        bfree(context->logo_frame_buffer);
        context->logo_frame_buffer = NULL;
    }

    C64_LOG_DEBUG("Logo system cleanup completed");
}

// Render logo to async video output (uses pre-rendered buffer)
void c64_logo_render_to_frame(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context || !context->logo_frame_buffer || !context->frame_buffer) {
        return;
    }

    // Copy pre-rendered logo to main frame buffer (very fast)
    size_t frame_size = context->width * context->height * sizeof(uint32_t);
    memcpy(context->frame_buffer, context->logo_frame_buffer, frame_size);

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

    // Very rare spot checks for logo rendering (every 10 minutes)
    static int logo_debug_count = 0;
    static uint64_t last_logo_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++logo_debug_count % 10000) == 0 ||
        (now - last_logo_log_time >= 600000000000ULL)) { // Every 10k renders OR 10 minutes
        C64_LOG_DEBUG("🔷 LOGO SPOT CHECK: %ux%u RGBA, timestamp=%" PRIu64 " (total count: %d)", obs_frame.width,
                      obs_frame.height, obs_frame.timestamp, logo_debug_count);
        last_logo_log_time = now;
    }
}

// Check if logo system is available and ready
bool c64_logo_is_available(struct c64_source *context)
{
    return context && context->logo_frame_buffer != NULL;
}
