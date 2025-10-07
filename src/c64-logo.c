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
#include "c64-logo.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-logging.h"
#include "c64-protocol.h"

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

    // C64 color scheme
    const uint32_t bg_color = 0xFF000080; // Dark blue background (RGBA)
    const uint32_t border_color =
        0xFF4040FF; // C64 blue border (RGBA)    // If logo texture is available, render it centered on dark background
    if (context->logo_texture) {
        uint32_t logo_width = gs_texture_get_width(context->logo_texture);
        uint32_t logo_height = gs_texture_get_height(context->logo_texture);

        // Fill background first
        for (uint32_t i = 0; i < width * height; i++) {
            buffer[i] = bg_color;
        }

        // Create centered logo area placeholder (actual logo texture reading needs graphics context)
        uint32_t logo_x = (width > logo_width) ? (width - logo_width) / 2 : 0;
        uint32_t logo_y = (height > logo_height) ? (height - logo_height) / 2 : 0;
        uint32_t logo_w = (logo_width < width) ? logo_width : width;
        uint32_t logo_h = (logo_height < height) ? logo_height : height;

        const uint32_t logo_bg = 0xFF202040; // Darker blue for logo area

        // Draw logo placeholder area with border
        for (uint32_t y = logo_y; y < logo_y + logo_h && y < height; y++) {
            for (uint32_t x = logo_x; x < logo_x + logo_w && x < width; x++) {
                if (x == logo_x || x == logo_x + logo_w - 1 || y == logo_y || y == logo_y + logo_h - 1) {
                    buffer[y * width + x] = border_color; // Border
                } else {
                    buffer[y * width + x] = logo_bg; // Logo area
                }
            }
        }

        C64_LOG_INFO("🔷 Pre-rendered logo frame: %ux%u logo at (%u,%u) in %ux%u frame", logo_w, logo_h, logo_x, logo_y,
                     width, height);
    } else {
        // Fallback: black screen if no logo texture (user requested no red/white pattern)
        memset(buffer, 0, width * height * sizeof(uint32_t));
        C64_LOG_INFO("🔷 Pre-rendered black screen fallback: %ux%u frame", width, height);
    }
    return true;
}

// Initialize logo system - load texture and pre-render frame
bool c64_logo_init(struct c64_source *context)
{
    if (!context) {
        return false;
    }

    C64_LOG_DEBUG("Initializing logo system...");

    // Load logo texture
    context->logo_texture = load_logo_texture();

    // Allocate pre-rendered logo frame buffer (same format as main frame buffer)
    size_t frame_size = context->width * context->height * sizeof(uint32_t);
    context->logo_frame_buffer = bmalloc(frame_size);
    if (!context->logo_frame_buffer) {
        C64_LOG_ERROR("Failed to allocate logo frame buffer (%zu bytes)", frame_size);
        if (context->logo_texture) {
            gs_texture_destroy(context->logo_texture);
            context->logo_texture = NULL;
        }
        return false;
    }

    // Pre-render the logo frame for instant availability
    if (!prerender_logo_frame(context)) {
        C64_LOG_WARNING("Failed to pre-render logo frame");
        bfree(context->logo_frame_buffer);
        context->logo_frame_buffer = NULL;
        if (context->logo_texture) {
            gs_texture_destroy(context->logo_texture);
            context->logo_texture = NULL;
        }
        return false;
    }

    C64_LOG_INFO("✅ Logo system initialized successfully");
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

// Render pre-rendered logo to async video output
void c64_logo_render_to_frame(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context || !context->logo_frame_buffer || !context->frame_buffer) {
        return;
    }

    // Simply copy pre-rendered logo to main frame buffer (super fast!)
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

    C64_LOG_DEBUG("🔷 Logo frame rendered: %ux%u RGBA, timestamp=%" PRIu64, obs_frame.width, obs_frame.height,
                  obs_frame.timestamp);
}

// Check if logo system is available and ready
bool c64_logo_is_available(struct c64_source *context)
{
    return context && context->logo_frame_buffer != NULL;
}
