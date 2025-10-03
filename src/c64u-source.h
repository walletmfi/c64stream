#ifndef C64U_SOURCE_H
#define C64U_SOURCE_H

#include <obs-module.h>

// Forward declarations
struct c64u_source;

// OBS source interface functions
void *c64u_create(obs_data_t *settings, obs_source_t *source);
void c64u_destroy(void *data);
void c64u_update(void *data, obs_data_t *settings);
void c64u_render(void *data, gs_effect_t *effect);
uint32_t c64u_get_width(void *data);
uint32_t c64u_get_height(void *data);
const char *c64u_get_name(void *unused);
obs_properties_t *c64u_properties(void *data);
void c64u_defaults(obs_data_t *settings);

// Streaming control functions
void c64u_start_streaming(struct c64u_source *context);
void c64u_stop_streaming(struct c64u_source *context);

#endif  // C64U_SOURCE_H
