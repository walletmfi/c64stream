/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-properties.h"
#include "c64-types.h"
#include "c64-version.h"
#include "c64-network.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-logging.h" // For Windows snprintf compatibility
#include "c64-file.h"
#include <obs-module.h>

// Forward declaration of callbacks
static bool crt_enable_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool crt_reset_defaults(obs_properties_t *props, obs_property_t *property, void *data);

obs_properties_t *c64_create_properties(void *data)
{
    UNUSED_PARAMETER(data);
    obs_properties_t *props = obs_properties_create();

    // Plugin Information Group
    obs_property_t *info_group =
        obs_properties_add_group(props, "info_group", "Plugin Information", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *info_props = obs_property_group_content(info_group);

    // Version information (read-only
    obs_property_t *version_prop = obs_properties_add_text(info_props, "version_info", "Version", OBS_TEXT_INFO);
    obs_property_set_long_description(version_prop, c64_get_build_info());
    obs_property_text_set_info_type(version_prop, OBS_TEXT_INFO_NORMAL);

    // Debug logging toggle
    obs_property_t *debug_prop = obs_properties_add_bool(info_props, "debug_logging", "Debug Logging");
    obs_property_set_long_description(debug_prop, "Enable detailed logging for debugging connection issues");

    // Network Configuration Group
    obs_property_t *network_group = obs_properties_add_group(props, "network_group", "Network Configuration",
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *network_props = obs_property_group_content(network_group);

    // DNS Server IP
    obs_property_t *dns_prop =
        obs_properties_add_text(network_props, "dns_server_ip", obs_module_text("DNSServerIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(dns_prop, obs_module_text("DNSServerIP.Description"));

    // C64 Ultimate Host (IP Address or Hostname)
    obs_property_t *host_prop =
        obs_properties_add_text(network_props, "c64_host", "C64 Ultimate Host", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(
        host_prop,
        "Hostname or IP address of C64 Ultimate device (default: c64u). Use 0.0.0.0 to skip control commands.");

    // OBS IP Address
    obs_property_t *obs_ip_prop =
        obs_properties_add_text(network_props, "obs_ip_address", "OBS Server IP", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(obs_ip_prop, "IP address of this OBS server (where C64 Ultimate sends streams)");

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop = obs_properties_add_bool(network_props, "auto_detect_ip", "Auto-detect OBS IP");
    obs_property_set_long_description(auto_ip_prop, "Automatically detect and use OBS server IP in streaming commands");

    // UDP Ports
    obs_property_t *video_port_prop = obs_properties_add_int(network_props, "video_port", "Video Port", 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, "UDP port for video stream from C64 Ultimate");

    obs_property_t *audio_port_prop = obs_properties_add_int(network_props, "audio_port", "Audio Port", 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, "UDP port for audio stream from C64 Ultimate");

    // Buffer Delay
    obs_property_t *delay_prop =
        obs_properties_add_int_slider(network_props, "buffer_delay_ms", "Buffer Delay (millis)", 0, 500, 1);
    obs_property_set_long_description(
        delay_prop,
        "Buffer network packets for specified milliseconds to smooth UDP packet loss/jitter (default: 10ms)");

    // Recording Group
    obs_property_t *recording_group =
        obs_properties_add_group(props, "recording_group", "Recording", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *recording_props = obs_property_group_content(recording_group);

    obs_property_t *save_frames_prop = obs_properties_add_bool(recording_props, "save_frames", "☐ Save BMP Frames");
    obs_property_set_long_description(
        save_frames_prop,
        "Save each frame as BMP in frames/ subfolder + CSV timing (for debugging - impacts performance)");

    obs_property_t *record_video_prop = obs_properties_add_bool(recording_props, "record_video", "☐ Record AVI + WAV");
    obs_property_set_long_description(
        record_video_prop, "Record uncompressed AVI video + WAV audio + CSV timing (for debugging - high disk usage)");

    // Save Folder
    obs_property_t *save_folder_prop =
        obs_properties_add_path(recording_props, "save_folder", "Output Folder", OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_long_description(
        save_folder_prop,
        "Directory where session folders with frames, video, audio, and timing files will be created");

    // Effects Group
    obs_property_t *effects_group =
        obs_properties_add_group(props, "effects_group", "Effects", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *effects_props = obs_property_group_content(effects_group);

    // Master control
    obs_property_t *crt_enable_prop = obs_properties_add_bool(effects_props, "crt_enable", "Enable CRT Visual Effects");
    obs_property_set_long_description(crt_enable_prop, "Enable or disable all CRT visual effects");
    obs_property_set_modified_callback(crt_enable_prop, crt_enable_modified);

    // Reset to defaults button
    obs_property_t *reset_button =
        obs_properties_add_button(effects_props, "crt_reset", "Reset to Defaults", crt_reset_defaults);
    obs_property_set_long_description(reset_button, "Reset all CRT effect settings to their default values");

    // Section label
    obs_properties_add_text(effects_props, "crt_label", "CRT EFFECT CONFIGURATION", OBS_TEXT_INFO);

    // Scanlines
    obs_properties_add_text(effects_props, "scanlines_label", "▶ Scanlines", OBS_TEXT_INFO);
    obs_property_t *scanlines_enable_prop =
        obs_properties_add_bool(effects_props, "scanlines_enable", "Enable Scanlines");
    obs_property_set_long_description(scanlines_enable_prop, "Enable horizontal scanline effect");

    obs_property_t *scanlines_opacity_prop =
        obs_properties_add_float_slider(effects_props, "scanlines_opacity", "Opacity", 0.0, 1.0, 0.05);
    obs_property_set_long_description(scanlines_opacity_prop,
                                      "Scanline darkness (0.0 = complete black, 1.0 = no separation)");

    obs_property_t *scanlines_width_prop =
        obs_properties_add_int_slider(effects_props, "scanlines_width", "Width (pixels)", 1, 6, 1);
    obs_property_set_long_description(scanlines_width_prop, "Height of black space between pixel rows");

    // Pixel Geometry
    obs_properties_add_text(effects_props, "pixel_geom_label", "▶ Pixel Geometry", OBS_TEXT_INFO);
    obs_property_t *pixel_width_prop =
        obs_properties_add_float_slider(effects_props, "pixel_width", "Pixel Width", 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, "Horizontal pixel size multiplier");

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", "Pixel Height", 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, "Vertical pixel size multiplier");

    // CRT Bloom
    obs_properties_add_text(effects_props, "bloom_label", "▶ CRT Bloom", OBS_TEXT_INFO);
    obs_property_t *bloom_enable_prop = obs_properties_add_bool(effects_props, "bloom_enable", "Enable Bloom");
    obs_property_set_long_description(bloom_enable_prop, "Enable brightness-based bloom effect");

    obs_property_t *bloom_strength_prop =
        obs_properties_add_float_slider(effects_props, "bloom_strength", "Strength", 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, "Bloom effect strength (0.0 = disabled, 1.0 = maximum)");

    return props;
}

// Callback for CRT enable checkbox - enables/disables all CRT effect controls
static bool crt_enable_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(property);
    bool enabled = obs_data_get_bool(settings, "crt_enable");

    // Get the effects group properties
    obs_property_t *effects_group = obs_properties_get(props, "effects_group");
    if (!effects_group)
        return true;

    obs_properties_t *effects_props = obs_property_group_content(effects_group);
    if (!effects_props)
        return true;

    // Enable/disable all effect sub-controls
    obs_property_t *reset_button = obs_properties_get(effects_props, "crt_reset");
    obs_property_t *scanlines_enable = obs_properties_get(effects_props, "scanlines_enable");
    obs_property_t *scanlines_opacity = obs_properties_get(effects_props, "scanlines_opacity");
    obs_property_t *scanlines_width = obs_properties_get(effects_props, "scanlines_width");
    obs_property_t *pixel_width = obs_properties_get(effects_props, "pixel_width");
    obs_property_t *pixel_height = obs_properties_get(effects_props, "pixel_height");
    obs_property_t *bloom_enable = obs_properties_get(effects_props, "bloom_enable");
    obs_property_t *bloom_strength = obs_properties_get(effects_props, "bloom_strength");

    if (reset_button)
        obs_property_set_enabled(reset_button, enabled);
    if (scanlines_enable)
        obs_property_set_enabled(scanlines_enable, enabled);
    if (scanlines_opacity)
        obs_property_set_enabled(scanlines_opacity, enabled);
    if (scanlines_width)
        obs_property_set_enabled(scanlines_width, enabled);
    if (pixel_width)
        obs_property_set_enabled(pixel_width, enabled);
    if (pixel_height)
        obs_property_set_enabled(pixel_height, enabled);
    if (bloom_enable)
        obs_property_set_enabled(bloom_enable, enabled);
    if (bloom_strength)
        obs_property_set_enabled(bloom_strength, enabled);

    return true;
}

// Callback for Reset to Defaults button
static bool crt_reset_defaults(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = data;
    if (!context)
        return false;

    // Get the settings object from the source
    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings)
        return false;

    // Reset all CRT effect settings to defaults
    obs_data_set_bool(settings, "scanlines_enable", false);
    obs_data_set_double(settings, "scanlines_opacity", 0.25);
    obs_data_set_int(settings, "scanlines_width", 1);
    obs_data_set_double(settings, "pixel_width", 1.0);
    obs_data_set_double(settings, "pixel_height", 1.0);
    obs_data_set_bool(settings, "bloom_enable", false);
    obs_data_set_double(settings, "bloom_strength", 0.25);

    // Apply the updated settings
    obs_source_update(context->source, settings);

    obs_data_release(settings);

    return true;
}

void c64_set_property_defaults(obs_data_t *settings)
{
    // Defaults initialization

    obs_data_set_default_bool(settings, "debug_logging", true);
    obs_data_set_default_bool(settings, "auto_detect_ip", true);
    obs_data_set_default_string(settings, "dns_server_ip", "192.168.1.1");
    obs_data_set_default_string(settings, "c64_host", C64_DEFAULT_HOST);
    obs_data_set_default_string(settings, "obs_ip_address", ""); // Empty by default, will be auto-detected
    obs_data_set_default_int(settings, "video_port", C64_DEFAULT_VIDEO_PORT);
    obs_data_set_default_int(settings, "audio_port", C64_DEFAULT_AUDIO_PORT);
    obs_data_set_default_int(settings, "buffer_delay_ms", 10); // Default 10ms buffer delay

    // Frame saving defaults
    obs_data_set_default_bool(settings, "save_frames", false); // Disabled by default

    // Platform-specific default recording folder (absolute paths to avoid tilde expansion issues)
    char platform_path[512];
    char documents_path[256];

    if (c64_get_user_documents_path(documents_path, sizeof(documents_path))) {
        // Use user's Documents folder
#ifdef _WIN32
        snprintf(platform_path, sizeof(platform_path), "%s\\obs-studio\\c64stream\\recordings", documents_path);
#else
        snprintf(platform_path, sizeof(platform_path), "%s/obs-studio/c64stream/recordings", documents_path);
#endif
    } else {
        // Fallback to platform-specific defaults
#ifdef _WIN32
        strcpy(platform_path, "C:\\Users\\Public\\Documents\\obs-studio\\c64stream\\recordings");
#elif defined(__APPLE__)
        strcpy(platform_path, "/Users/user/Documents/obs-studio/c64stream/recordings");
#else // Linux and other Unix-like systems
        strcpy(platform_path, "/home/user/Documents/obs-studio/c64stream/recordings");
#endif
    }

    obs_data_set_default_string(settings, "save_folder", platform_path);

    // Video recording defaults
    obs_data_set_default_bool(settings, "record_video", false); // Disabled by default

    // CRT effects defaults
    obs_data_set_default_bool(settings, "crt_enable", false);
    obs_data_set_default_bool(settings, "scanlines_enable", false);
    obs_data_set_default_double(settings, "scanlines_opacity", 0.25);
    obs_data_set_default_int(settings, "scanlines_width", 1);
    obs_data_set_default_double(settings, "pixel_width", 1.0);
    obs_data_set_default_double(settings, "pixel_height", 1.0);
    obs_data_set_default_bool(settings, "bloom_enable", false);
    obs_data_set_default_double(settings, "bloom_strength", 0.25);
}
