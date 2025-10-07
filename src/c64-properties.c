#include "c64-properties.h"
#include "c64-types.h"
#include "c64-version.h"
#include "c64-network.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-logging.h" // For Windows snprintf compatibility
#include <obs-module.h>

obs_properties_t *c64_create_properties(void *data)
{
    // C64S properties setup
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();

    // Plugin Information Group (at the top)
    obs_property_t *info_group =
        obs_properties_add_group(props, "info_group", "Plugin Information", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *info_props = obs_property_group_content(info_group);

    // Version information (read-only) - remove redundant "Plugin Version" text
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

    // DNS Server IP (first property in network group)
    obs_property_t *dns_prop =
        obs_properties_add_text(network_props, "dns_server_ip", obs_module_text("DNSServerIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(dns_prop, obs_module_text("DNSServerIP.Description"));

    // C64 Ultimate Host (IP Address or Hostname)
    obs_property_t *host_prop =
        obs_properties_add_text(network_props, "c64_host", "C64 Ultimate Host", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(
        host_prop,
        "Hostname or IP address of C64 Ultimate device (default: c64u). Use 0.0.0.0 to skip control commands.");

    // OBS IP Address (editable)
    obs_property_t *obs_ip_prop =
        obs_properties_add_text(network_props, "obs_ip_address", "OBS Server IP", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(obs_ip_prop, "IP address of this OBS server (where C64 Ultimate sends streams)");

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop = obs_properties_add_bool(network_props, "auto_detect_ip", "Auto-detect OBS IP");
    obs_property_set_long_description(auto_ip_prop, "Automatically detect and use OBS server IP in streaming commands");

    // UDP Ports within the same network group
    obs_property_t *video_port_prop = obs_properties_add_int(network_props, "video_port", "Video Port", 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, "UDP port for video stream from C64 Ultimate");

    obs_property_t *audio_port_prop = obs_properties_add_int(network_props, "audio_port", "Audio Port", 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, "UDP port for audio stream from C64 Ultimate");

    // Buffer Delay (moved to Plugin Information group)
    obs_property_t *delay_prop =
        obs_properties_add_int_slider(info_props, "buffer_delay_ms", "Buffer Delay (millis)", 0, 2000, 5);
    obs_property_set_long_description(
        delay_prop,
        "Buffer network packets for specified milliseconds to smooth UDP packet loss/jitter (default: 10ms)");

    // Recording Group (compact layout)
    obs_property_t *recording_group =
        obs_properties_add_group(props, "recording_group", "Recording", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *recording_props = obs_property_group_content(recording_group);

    obs_property_t *save_frames_prop = obs_properties_add_bool(recording_props, "save_frames", "☐ Save BMP Frames");
    obs_property_set_long_description(
        save_frames_prop, "Save each frame as BMP in frames/ subfolder (for debugging - impacts performance)");

    obs_property_t *record_video_prop = obs_properties_add_bool(recording_props, "record_video", "☐ Record AVI + WAV");
    obs_property_set_long_description(record_video_prop,
                                      "Record uncompressed AVI video + WAV audio (for debugging - high disk usage)");

    // Save Folder (applies to both frame saving and video recording) - now properly in Recording group
    obs_property_t *save_folder_prop =
        obs_properties_add_path(recording_props, "save_folder", "Output Folder", OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_long_description(
        save_folder_prop,
        "Directory where session folders with frames, video, audio, and timing files will be created");

    return props;
}

void c64_set_property_defaults(obs_data_t *settings)
{
    // C64S defaults initialization

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
        // Successfully got user's Documents folder
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
}
