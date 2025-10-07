#ifndef C64_LOGO_H
#define C64_LOGO_H

#include <stdint.h>
#include <stdbool.h>
#include <obs-module.h>
#include <graphics/graphics.h>

// Forward declarations
struct c64_source;

// Logo management functions
bool c64_logo_init(struct c64_source *context);
void c64_logo_cleanup(struct c64_source *context);
void c64_logo_render_to_frame(struct c64_source *context, uint64_t timestamp_ns);
bool c64_logo_is_available(struct c64_source *context);

#endif // C64_LOGO_H
