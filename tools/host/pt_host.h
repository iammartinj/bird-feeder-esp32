// Writes an RGB565 picture (memory order) as a PNG, for the preview on a
// computer. Uncompressed, no dependencies.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nonzero on success.
int pt_host_write_png_sized(const char *path, const uint16_t *pixels, int w, int h);

#ifdef __cplusplus
}
#endif
