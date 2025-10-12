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
#include "c64-presets.h"
#include <obs-module.h>

// Forward declaration of callbacks
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

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

    // Presets dropdown at the top
    obs_property_t *preset_prop =
        obs_properties_add_list(effects_props, "crt_preset", "Presets", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(preset_prop,
                                      "Select a visual effect preset to apply predefined CRT simulation settings");

    // Populate presets from the loaded presets file
    c64_presets_populate_list(preset_prop);

    // Add modified callback to apply preset when selected
    obs_property_set_modified_callback(preset_prop, crt_preset_changed);

    // Scanlines
    obs_property_t *scanline_distance_prop = obs_properties_add_list(
        effects_props, "scan_line_distance", "Scan Line Distance", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
    obs_property_list_add_float(scanline_distance_prop, "None (0%)", 0.0);
    obs_property_list_add_float(scanline_distance_prop, "Tight (25%)", 0.25);
    obs_property_list_add_float(scanline_distance_prop, "Normal (50%)", 0.50);
    obs_property_list_add_float(scanline_distance_prop, "Wide (100%)", 1.0);
    obs_property_list_add_float(scanline_distance_prop, "Extra Wide (200%)", 2.0);
    obs_property_set_long_description(
        scanline_distance_prop,
        "Gap between consecutive scan lines as a percentage of a scan line width. Controls the spacing between bright "
        "scan lines. 0% = no gaps (continuous lines), 100% = gap equals line height.");

    obs_property_t *scanline_strength_prop =
        obs_properties_add_float_slider(effects_props, "scan_line_strength", "Scan Line Strength", 0.0, 1.0, 0.05);
    obs_property_set_long_description(
        scanline_strength_prop,
        "Controls how dark the gaps between scan lines appear. 0.0 = gaps are as bright as scan lines (no effect), "
        "1.0 = gaps are completely black.");

    // Pixel Geometry
    obs_property_t *pixel_width_prop =
        obs_properties_add_float_slider(effects_props, "pixel_width", "Pixel Width", 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, "Horizontal pixel size multiplier");

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", "Pixel Height", 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, "Vertical pixel size multiplier");

    obs_property_t *blur_strength_prop =
        obs_properties_add_float_slider(effects_props, "blur_strength", "Blur", 0.0, 1.0, 0.05);
    obs_property_set_long_description(blur_strength_prop,
                                      "Scaling blur (0.0 = precise scaling, 1.0 = very blurry with gaussian blur)");

    // CRT Bloom
    obs_property_t *bloom_strength_prop =
        obs_properties_add_float_slider(effects_props, "bloom_strength", "Bloom", 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, "Bloom effect strength (0.0 = off, 1.0 = maximum)");

    // Afterglow
    obs_property_t *afterglow_duration_prop =
        obs_properties_add_int_slider(effects_props, "afterglow_duration_ms", "Afterglow Duration (ms)", 0, 3000, 10);
    obs_property_set_long_description(afterglow_duration_prop, "Phosphor persistence duration (0 = off)");

    obs_property_t *afterglow_curve_prop = obs_properties_add_list(effects_props, "afterglow_curve", "Afterglow Curve",
                                                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(afterglow_curve_prop, "Instant Fade (Linear)", 0);
    obs_property_list_add_int(afterglow_curve_prop, "Gradual Fade (Slow Start)", 1);
    obs_property_list_add_int(afterglow_curve_prop, "Rapid Fade (Fast Start)", 2);
    obs_property_list_add_int(afterglow_curve_prop, "Long Tail (Exponential)", 3);
    obs_property_set_long_description(afterglow_curve_prop, "How quickly the afterglow fades away");

    // Screen Tint
    obs_property_t *tint_mode_prop =
        obs_properties_add_list(effects_props, "tint_mode", "Tint Type", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(tint_mode_prop, "None (colour)", 0);
    obs_property_list_add_int(tint_mode_prop, "Amber", 1);
    obs_property_list_add_int(tint_mode_prop, "Green", 2);
    obs_property_list_add_int(tint_mode_prop, "Monochrome", 3);
    obs_property_set_long_description(tint_mode_prop, "Type of monochrome tint to apply");

    obs_property_t *tint_strength_prop =
        obs_properties_add_float_slider(effects_props, "tint_strength", "Tint Strength", 0.0, 1.0, 0.05);
    obs_property_set_long_description(tint_strength_prop, "Strength of tint effect (0.0 = off, 1.0 = full tint)");

    return props;
}

// Callback for preset selection
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings)
        return false;

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    if (!preset_name || preset_name[0] == '\0')
        return false;

    // Apply the preset
    if (c64_presets_apply(settings, preset_name)) {
        C64_LOG_INFO("Applied CRT preset: %s", preset_name);
        return true;
    }

    return false;
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
    obs_data_set_default_double(settings, "scan_line_distance", 0.0);
    obs_data_set_default_double(settings, "scan_line_strength", 0.0);
    obs_data_set_default_double(settings, "pixel_width", 1.0);
    obs_data_set_default_double(settings, "pixel_height", 1.0);
    obs_data_set_default_double(settings, "blur_strength", 0.0);
    obs_data_set_default_double(settings, "bloom_strength", 0.0);
    obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
    obs_data_set_default_int(settings, "afterglow_curve", 0);
    obs_data_set_default_int(settings, "tint_mode", 0);
    obs_data_set_default_double(settings, "tint_strength", 0.0);
}
