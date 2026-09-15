#pragma GCC optimize("O3")
#include "snow.h"
#include "gfx.h"
#include "sky.h"
#include <math.h>

static const int MAX_FLAKES = 180;
static const int FLAKES[3] = {0, 70, 170};

struct Flake { float x, y, vy, big, ph; };

EXT_RAM_BSS_ATTR static Flake s_flakes[MAX_FLAKES];
static int s_n;
static float s_wind, s_t;
static uint32_t s_rng = 1;

static float frand() {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return (float)(s_rng >> 8) / 16777216.0f;
}

void snowInit(int density, float wind, uint32_t seed) {
  s_rng = seed ? seed : 1;
  s_n = FLAKES[density < 0 ? 0 : (density > 2 ? 2 : density)];
  s_wind = wind;
  s_t = 0;
  for (int i = 0; i < s_n; i++) {
    Flake& f = s_flakes[i];
    f.x = frand() * SCR_W;
    f.y = frand() * SCR_H;
    f.big = frand() < 0.25f ? 1.0f : 0.0f;
    f.vy = f.big ? 14.0f + frand() * 8.0f : 8.0f + frand() * 6.0f;   // nearer ones fall faster
    f.ph = frand() * 6.283f;
  }
}

void snowStep(float dt) {
  s_t += dt;
  for (int i = 0; i < s_n; i++) {
    Flake& f = s_flakes[i];
    f.x += (s_wind * (0.6f + 0.4f * f.big) + sinf(s_t * 0.8f + f.ph) * 5.0f) * dt;
    f.y += f.vy * dt;
    if (f.y > SCR_H + 2) { f.y = -2; f.x = frand() * SCR_W; }
    if (f.x < -2) f.x += SCR_W + 4;
    if (f.x > SCR_W + 2) f.x -= SCR_W + 4;
  }
}

// a flake is a point spread over the four pixels around it
static inline void splat(float x, float y, uint16_t c, float a) {
  const int ix = (int)floorf(x), iy = (int)floorf(y);
  const float fx = x - ix, fy = y - iy;
  px_blend(ix, iy, c, (int)(a * (1 - fx) * (1 - fy) * 32 + 0.5f));
  px_blend(ix + 1, iy, c, (int)(a * fx * (1 - fy) * 32 + 0.5f));
  px_blend(ix, iy + 1, c, (int)(a * (1 - fx) * fy * 32 + 0.5f));
  px_blend(ix + 1, iy + 1, c, (int)(a * fx * fy * 32 + 0.5f));
}

void snowDraw(int x0, int x1) {
  if (!s_n) return;
  const int v = (int)(240.0f * skyLevel());
  const uint16_t c = rgb565(v, v, v > 12 ? v - 6 : v);
  for (int i = 0; i < s_n; i++) {
    const Flake& f = s_flakes[i];
    if (f.x < x0 - 2 || f.x >= x1 + 1) continue;
    if (f.big) {
      splat(f.x, f.y, c, 0.85f);
      splat(f.x + 0.7f, f.y + 0.5f, c, 0.45f);
    } else {
      splat(f.x, f.y, c, 0.55f);
    }
  }
}
