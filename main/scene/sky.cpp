#pragma GCC optimize("O3")
#include "sky.h"
#include "feeder_art.h"
#include "feeder_bg.h"
#include "gfx.h"
#include <string.h>
#include <math.h>

static const float NIGHT_LEVEL = 0.12f;
static const float TRANSITION_H = 40.0f / 60.0f;
static const float WARM_H = 3.0f;            // how long morning and evening light last
#ifdef FEEDER_NO_CLOUDS                      // tools/video.cpp: on a big monitor they read as stripes
static const float CLOUD_AMP = 0.0f;
static const float SUN_AMP = 0.0f;
#else
static const float CLOUD_AMP = 0.08f;
static const float SUN_AMP = 0.10f;
#endif
static const float CLOUD_WAVES = 0.8f;       // wave crests across the picture
static const float SUN_WIDTH = 45.0f;        // px
static const float SUN_TRAVEL_S = 90.0f;
static const float SUN_BREAK_S = 37.0f;
static const float SPRITE_LIGHT_MIX = 0.30f;

static const float NEUTRAL[3] = {1.00f, 1.00f, 1.00f};
static const float MORNING[3] = {1.06f, 1.00f, 0.86f};
static const float EVENING[3] = {1.08f, 0.96f, 0.78f};
static const float NIGHT[3]   = {0.80f, 0.92f, 1.12f};

static int   s_bg;
static bool  s_winter;
static float s_period = 40.0f, s_dir = 1.0f;
static float s_hour = 12.0f;
static float s_phase, s_sunT;
static float s_level = 1.0f;
static float s_tint[3] = {1, 1, 1};           // colour times level
static float s_col[SCR_W];                    // cloud and sun, per column
static int   s_tr = 256, s_tg = 256, s_tb = 256;
static uint8_t s_maskY0[SCR_W], s_maskY1[SCR_W];   // rows of the branch in each column, [y0, y1)

static void lerp3(const float* a, const float* b, float t, float* out) {
  for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}

static float dawnHour() { return s_winter ? 7.0f : 5.0f; }
static float duskHour() { return s_winter ? 17.0f : 20.0f; }

bool skyNight() {
  return s_hour < dawnHour() || s_hour >= duskHour();
}

// Night is dusk .. dawn (the brief's simplification of civil twilight); the
// brightness comes up over the 40 minutes after dawn and goes down over the 40
// before dusk.
static void dayColour() {
  const float dawn = dawnHour(), dusk = duskHour(), h = s_hour;
  float c[3];
  if (h < dawn || h >= dusk) {
    s_level = NIGHT_LEVEL;
    lerp3(NIGHT, NIGHT, 0, c);
  } else if (h < dawn + TRANSITION_H) {
    const float t = (h - dawn) / TRANSITION_H;
    s_level = NIGHT_LEVEL + (1.0f - NIGHT_LEVEL) * t;
    lerp3(NIGHT, MORNING, t, c);
  } else if (h < dawn + WARM_H) {
    s_level = 1.0f;
    lerp3(MORNING, NEUTRAL, (h - dawn - TRANSITION_H) / (WARM_H - TRANSITION_H), c);
  } else if (h < dusk - WARM_H) {
    s_level = 1.0f;
    lerp3(NEUTRAL, NEUTRAL, 0, c);
  } else if (h < dusk - TRANSITION_H) {
    s_level = 1.0f;
    lerp3(NEUTRAL, EVENING, (h - (dusk - WARM_H)) / (WARM_H - TRANSITION_H), c);
  } else {
    const float t = (h - (dusk - TRANSITION_H)) / TRANSITION_H;
    s_level = 1.0f + (NIGHT_LEVEL - 1.0f) * t;
    lerp3(EVENING, NIGHT, t, c);
  }
  for (int i = 0; i < 3; i++) s_tint[i] = c[i] * s_level;
  s_tr = (int)(s_tint[0] * 256.0f);
  s_tg = (int)(s_tint[1] * 256.0f);
  s_tb = (int)(s_tint[2] * 256.0f);
}

void skyInit(int background, bool winter, float cloudPeriodS, float cloudDir) {
  s_bg = background;
  s_winter = winter;
  s_period = cloudPeriodS;
  s_dir = cloudDir;
  s_phase = 0;
  s_sunT = 0;
  for (int x = 0; x < SCR_W; x++) {
    int y0 = FB_H, y1 = 0;
    for (int y = 0; y < FB_H; y++) {
      if (!BRANCH_MASK[y * SCR_W + x]) continue;
      if (y < y0) y0 = y;
      y1 = y + 1;
    }
    s_maskY0[x] = (uint8_t)(y0 < y1 ? y0 : 0);
    s_maskY1[x] = (uint8_t)y1;
  }
  dayColour();
  skyStep(0);
}

void skySetClock(float hour) {
  s_hour = hour;
  dayColour();
}

float skyLevel() { return s_level; }

void skyStep(float dt) {
  const float TAU = 6.2831853f;
  s_phase += dt * TAU / s_period * s_dir;
  if (s_phase > TAU) s_phase -= TAU;
  if (s_phase < 0) s_phase += TAU;
  s_sunT += dt;
  if (s_sunT > SUN_TRAVEL_S * SUN_BREAK_S) s_sunT = 0;
  const float sunX = 160.0f + 200.0f * sinf(s_sunT * TAU / SUN_TRAVEL_S);
  const float sunK = 0.5f + 0.5f * sinf(s_sunT * TAU / SUN_BREAK_S);
  for (int x = 0; x < SCR_W; x++) {
    float g = 1.0f + CLOUD_AMP * sinf(s_phase + TAU * CLOUD_WAVES * (float)x / SCR_W);
    if (s_bg == BG_WINTER_MORNING) {
      const float d = ((float)x - sunX) / SUN_WIDTH;
      g += SUN_AMP * sunK * expf(-d * d);
    }
    s_col[x] = g;
  }
}

void IRAM_ATTR skyApply(int x0, int x1, const float* disp) {
  memcpy(FB + (x0 - gBandX0) * FB_H, BGBUF + x0 * FB_H, (size_t)(x1 - x0) * FB_H * sizeof(uint16_t));
  for (int x = x0; x < x1; x++) {
    const int dq = (int)(disp[x] * 256.0f);   // 1/256 px
    if (s_maskY1[x] <= s_maskY0[x] || (dq < 8 && dq > -8)) continue;
    const uint16_t* src = BGBUF + x * FB_H;
    uint16_t* dst = FB + (x - gBandX0) * FB_H;
    for (int y = s_maskY0[x]; y < s_maskY1[x]; y++) {
      const int m = BRANCH_MASK[y * SCR_W + x];
      if (!m) continue;
      const int sy = (y << 8) - ((dq * m) >> 8);   // this row shows the photo from sy
      int iy = sy >> 8;
      const int f = (sy & 0xFF) >> 4, nf = 16 - f;
      if (iy < 0) iy = 0;
      if (iy > FB_H - 2) iy = FB_H - 2;
      const uint16_t a = src[FB_H - 1 - iy], b = src[FB_H - 2 - iy];
      const int r = (((a >> 11) * nf + (b >> 11) * f) >> 4);
      const int g = ((((a >> 5) & 0x3F) * nf + ((b >> 5) & 0x3F) * f) >> 4);
      const int bl = (((a & 0x1F) * nf + (b & 0x1F) * f) >> 4);
      dst[FB_H - 1 - y] = (uint16_t)((r << 11) | (g << 5) | bl);
    }
  }
  for (int x = x0; x < x1; x++) {
    const int gc = (int)(s_col[x] * 256.0f);
    const int gr = (s_tr * gc) >> 8, gg = (s_tg * gc) >> 8, gb = (s_tb * gc) >> 8;
    uint16_t* p = FB + (x - gBandX0) * FB_H;
    for (int i = 0; i < FB_H; i++) {
      const uint16_t c = p[i];
      int r = ((c >> 11) * gr) >> 8;
      int g = (((c >> 5) & 0x3F) * gg) >> 8;
      int b = ((c & 0x1F) * gb) >> 8;
      if (r > 31) r = 31;
      if (g > 63) g = 63;
      if (b > 31) b = 31;
      p[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
}

void skySpriteGain(float x, float rgb[3]) {
  int ix = (int)x;
  ix = ix < 0 ? 0 : (ix >= SCR_W ? SCR_W - 1 : ix);
  const float mix = 1.0f + (s_col[ix] - 1.0f) * SPRITE_LIGHT_MIX;
  for (int i = 0; i < 3; i++) rgb[i] = s_tint[i] * mix;
}
