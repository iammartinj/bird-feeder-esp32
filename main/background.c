// The garden photo: a baked 320x240 JPEG decoded once into PSRAM, dithered into
// RGB565 and laid out column by column the way the scene draws it.
#include "background.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

// stb_image, public domain / MIT (github.com/nothings/stb). JPEG only, every
// allocation in PSRAM.
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p, sz) heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) heap_caps_free(p)
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "stb_image.h"
#pragma GCC diagnostic pop

static const char *TAG = "background";

// A 4 x 4 ordered dither: a photograph in RGB565 bands without one.
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static inline uint16_t dither565(int r, int g, int b, int x, int y)
{
    const int t = BAYER[y & 3][x & 3];
    r += (t * 8) / 16 - 4;
    g += (t * 4) / 16 - 2;
    b += (t * 8) / 16 - 4;
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t *background_decode(const uint8_t *jpeg, size_t len, int w, int h)
{
    int iw = 0, ih = 0, comp = 0;
    uint8_t *rgb = stbi_load_from_memory(jpeg, (int)len, &iw, &ih, &comp, 3);
    if (!rgb) {
        ESP_LOGE(TAG, "would not decode: %s", stbi_failure_reason());
        return NULL;
    }
    if (iw != w || ih != h) {
        ESP_LOGE(TAG, "%d x %d, wanted %d x %d", iw, ih, w, h);
        stbi_image_free(rgb);
        return NULL;
    }
    uint16_t *out = heap_caps_malloc((size_t)w * (size_t)h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (out) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const uint8_t *p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
                out[(size_t)x * (size_t)h + (size_t)(h - 1 - y)] = dither565(p[0], p[1], p[2], x, y);
            }
        }
    }
    stbi_image_free(rgb);
    return out;
}
