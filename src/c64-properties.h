#ifndef C64_PROPERTIES_H
#define C64_PROPERTIES_H

#include <obs-module.h>

/**
 * Create and populate OBS properties for the C64 Ultimate source
 * @param data Source data (unused, can be NULL)
 * @return Populated properties structure
 */
obs_properties_t *c64_create_properties(void *data);

/**
 * Set default values for all C64 Ultimate source properties
 * @param settings Settings object to populate with defaults
 */
void c64_set_property_defaults(obs_data_t *settings);

#endif  // C64_PROPERTIES_H
