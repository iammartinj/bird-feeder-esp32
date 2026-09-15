#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A w x h JPEG as RGB565 in PSRAM, laid out by columns bottom-up
// (x * h + (h - 1 - y)), which is the scene's BGBUF. NULL on failure.
uint16_t *background_decode(const uint8_t *jpeg, size_t len, int w, int h);

#ifdef __cplusplus
}
#endif
