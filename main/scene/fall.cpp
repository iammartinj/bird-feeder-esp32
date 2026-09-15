#pragma GCC optimize("O3")
#include "fall.h"
#include "feeder_art.h"
#include "gfx.h"
#include "sky.h"
#include <math.h>

// --- leaves ------------------------------------------------------------------
static const int MAX_LEAVES = 12;
static const float LEAF_GAP_S = 90.0f;           // mean time between leaves
static const float LEAF_GAP_MIN_S = 20.0f, LEAF_GAP_MAX_S = 240.0f;
static const float GUST_RATE = 3.0f;             // a gust brings them this much more often
static const float NEAR_LEAF_CHANCE = 0.2f;
static const float HAZE_FAR = 0.30f;
static const int HAZE_R = 200, HAZE_G = 206, HAZE_B = 214;
// from the leaves on bg_autumn_empty.png's ground, a little warmer: a leaf in
// the air catches the light
static const uint8_t LEAF_RGB[5][3] = {{150, 82, 40}, {172, 108, 52}, {118, 68, 38}, {186, 132, 72}, {98, 60, 36}};

// --- far snow ------------------------------------------------------------------
static const int MAX_FAR_FLAKES = 160;
static const float FLURRY_GAP_MIN_S = 180.0f, FLURRY_GAP_MAX_S = 480.0f;
static const float FLURRY_MIN_S = 20.0f, FLURRY_MAX_S = 60.0f;
static const float FLURRY_RAMP_S = 6.0f;
static const float FLAKES_PER_S = 70.0f;         // at full strength, doubled in a gust

struct Leaf {
  bool  on, near;
  uint8_t col;
  float x, y, vy, len, ph, spin, spinPh, land, fade;
};
struct FarFlake {
  bool  on;
  float x, y, vy, ph, land, fade;
};

static Leaf s_leaves[MAX_LEAVES];
EXT_RAM_BSS_ATTR static FarFlake s_far[MAX_FAR_FLAKES];
static bool s_autumn;
static float s_t, s_nextLeaf, s_nextFlurry, s_flurryT, s_flurryLen, s_spawn;
static float s_wind;
static bool s_windy;
static uint32_t s_rng = 1;

static float frand() {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return (float)(s_rng >> 8) / 16777216.0f;
}
static float rnd(float a, float b) { return a + (b - a) * frand(); }
static float gap(float mean, float lo, float hi) {
  const float g = -log(1.0f - frand() * 0.999f) * mean;
  return g < lo ? lo : (g > hi ? hi : g);
}
// how much of a gust there is right now, 0 .. 1
static float gustiness() {
  const float g = fabsf(s_wind) - 0.8f;
  return g < 0 ? 0 : (g > 1 ? 1 : g);
}

void fallInit(bool autumn, uint32_t seed) {
  s_rng = seed ? seed : 1;
  s_autumn = autumn;
  s_t = 0;
  s_wind = 0;
  for (int i = 0; i < MAX_LEAVES; i++) s_leaves[i].on = false;
  for (int i = 0; i < MAX_FAR_FLAKES; i++) s_far[i].on = false;
  s_nextLeaf = rnd(10.0f, 60.0f);
  s_nextFlurry = rnd(30.0f, FLURRY_GAP_MAX_S);
  s_flurryT = s_flurryLen = 0;
  s_spawn = 0;
}

static void addLeaf(bool near, float delayPx) {
  for (int i = 0; i < MAX_LEAVES; i++) {
    Leaf& l = s_leaves[i];
    if (l.on) continue;
    l.on = true;
    l.near = near;
    l.col = (uint8_t)(frand() * 4.99f);
    l.ph = rnd(0, 6.283f);
    l.spin = rnd(2.5f, 5.0f);
    l.spinPh = rnd(0, 6.283f);
    // upwind of where it will be, so the wind carries it across
    l.x = rnd(0.0f, (float)SCR_W) - s_wind * (near ? 40.0f : 25.0f);
    if (near) {
      l.y = -20.0f - delayPx;
      l.vy = rnd(45.0f, 60.0f);
      l.len = rnd(10.0f, 14.0f);
      l.land = SCR_H + 30.0f;
      l.fade = 1.0f;
    } else {
      l.y = rnd(-4.0f, 40.0f) - delayPx;
      l.vy = rnd(9.0f, 15.0f);
      l.len = rnd(2.4f, 3.6f);
      l.land = rnd(150.0f, 232.0f);
      l.fade = 0.0f;
    }
    return;
  }
}

static void leafEvent(bool nearOnly) {
  if (nearOnly || frand() < NEAR_LEAF_CHANCE) {
    addLeaf(true, 0);
    return;
  }
  const int n = 1 + (gustiness() > 0.3f ? (int)(frand() * 2.99f) : (frand() < 0.3f ? 1 : 0));
  for (int k = 0; k < n; k++) addLeaf(false, k * rnd(6.0f, 16.0f));
}

void fallForce(FallForce what) {
  if (what == FALL_LEAVES) leafEvent(false);
  else if (what == FALL_NEAR_LEAF) leafEvent(true);
  else { s_flurryLen = rnd(FLURRY_MIN_S, FLURRY_MAX_S); s_flurryT = 0.001f; }
}

void fallStep(float dt, float wind, bool windy) {
  s_t += dt;
  s_wind = wind;
  s_windy = windy;
  const float drift = wind * (windy ? 14.0f : 6.0f);

  if (s_autumn) {
    s_nextLeaf -= dt * (1.0f + (GUST_RATE - 1.0f) * gustiness());
    if (s_nextLeaf <= 0) {
      leafEvent(false);
      s_nextLeaf = gap(LEAF_GAP_S, LEAF_GAP_MIN_S, LEAF_GAP_MAX_S);
    }
  } else if (s_flurryT <= 0) {
    s_nextFlurry -= dt;
    if (s_nextFlurry <= 0) {
      fallForce(FALL_FLURRY);
      s_nextFlurry = rnd(FLURRY_GAP_MIN_S, FLURRY_GAP_MAX_S);
    }
  }

  for (int i = 0; i < MAX_LEAVES; i++) {
    Leaf& l = s_leaves[i];
    if (!l.on) continue;
    const float k = l.near ? 2.5f : 1.0f;
    l.x += (drift * k + sinf(s_t * 1.3f + l.ph) * (l.near ? 18.0f : 6.0f)) * dt;
    if (l.y < l.land) {
      l.y += l.vy * (0.7f + 0.3f * sinf(s_t * 2.6f + l.ph)) * dt;   // it flutters, catching the air
      if (!l.near && l.y > -2.0f) l.fade = l.fade + dt / 0.6f > 1 ? 1 : l.fade + dt / 0.6f;
    } else if (l.near) {
      l.on = false;
    } else {
      l.y = l.land;                   // lies there a moment, then is one of the many
      l.fade -= dt / 1.5f;
      if (l.fade <= 0) l.on = false;
    }
    if (l.x < -30.0f || l.x > SCR_W + 30.0f) l.on = false;
  }

  // the flurry: comes on, holds, thins out
  float strength = 0;
  if (s_flurryT > 0) {
    s_flurryT += dt;
    const float left = s_flurryLen - s_flurryT;
    strength = s_flurryT / FLURRY_RAMP_S;
    if (left / FLURRY_RAMP_S < strength) strength = left / FLURRY_RAMP_S;
    if (strength > 1) strength = 1;
    if (left <= 0) s_flurryT = 0;
  }
  s_spawn += strength * FLAKES_PER_S * (1.0f + gustiness()) * dt;
  for (int i = 0; i < MAX_FAR_FLAKES && s_spawn >= 1.0f; i++) {
    FarFlake& f = s_far[i];
    if (f.on) continue;
    s_spawn -= 1.0f;
    f.on = true;
    f.x = rnd(-20.0f, SCR_W + 20.0f);
    f.y = rnd(-4.0f, 130.0f);
    f.vy = rnd(7.0f, 12.0f);
    f.ph = rnd(0, 6.283f);
    f.land = rnd(150.0f, 236.0f);
    f.fade = 0;
  }
  if (s_spawn > 1.0f) s_spawn = 1.0f;
  for (int i = 0; i < MAX_FAR_FLAKES; i++) {
    FarFlake& f = s_far[i];
    if (!f.on) continue;
    f.x += (drift * 0.8f + sinf(s_t * 0.9f + f.ph) * 3.0f) * dt;
    if (f.y < f.land) {
      f.y += f.vy * dt;
      f.fade = f.fade + dt / 0.4f > 1 ? 1 : f.fade + dt / 0.4f;
    } else {
      f.fade -= dt / 0.5f;
      if (f.fade <= 0) f.on = false;
    }
    if (f.x < -25.0f || f.x > SCR_W + 25.0f) f.on = false;
  }
}

// --- drawing ----------------------------------------------------------------------
struct LeafShape { float half, w, c, s; uint8_t r, g, b; };

static LeafShape leafShape(const Leaf& l, float scale) {
  LeafShape k;
  k.half = l.len * 0.5f * scale;
  const float flip = cosf(s_t * l.spin + l.spinPh);          // the leaf turning over
  k.w = k.half * 0.55f * (fabsf(flip) > 0.2f ? fabsf(flip) : 0.2f);
  const float a = 0.7f * sinf(s_t * 1.7f + l.ph) + l.ph;
  k.c = cosf(a);
  k.s = sinf(a);
  float lg[3];
  skySpriteGain(l.x, lg);
  const float under = flip < 0 ? 0.8f : 1.0f;                // the paler, duller back of it
  const float haze = l.near ? 0.0f : HAZE_FAR;
  const int hz[3] = {HAZE_R, HAZE_G, HAZE_B};
  uint8_t* out[3] = {&k.r, &k.g, &k.b};
  for (int i = 0; i < 3; i++) {
    float v = (LEAF_RGB[l.col][i] * under * (1 - haze) + hz[i] * haze) * lg[i];
    *out[i] = (uint8_t)(v > 255 ? 255 : v);
  }
  return k;
}

// In front: a polygon, nothing covers it.
static void drawNearLeaf(const Leaf& l, float scale, float alpha, int x0, int x1) {
  const LeafShape k = leafShape(l, scale);
  if (l.x + k.half + 2 < x0 || l.x - k.half - 2 >= x1) return;
  float xs[8], ys[8];
  for (int i = 0; i < 8; i++) {
    const float t = (float)i * (6.2831853f / 8.0f);
    const float ex = cosf(t) * k.half, ey = sinf(t) * k.w;
    xs[i] = l.x + ex * k.c - ey * k.s;
    ys[i] = l.y + ex * k.s + ey * k.c;
  }
  PolyPaint p;
  p.r0 = k.r;
  p.g0 = k.g;
  p.b0 = k.b;
  p.a0 = alpha;
  fillPolyAA(xs, ys, 8, p);
}

// How much of the branch is in front of scene pixel (x, y), 0..255: the wood
// mask where the photo there came from, the bend being disp[x] down.
static inline int wood(int x, int y, const float* disp) {
  if ((unsigned)x >= (unsigned)SCR_W) return 0;
  const int yy = y - (int)floorf(disp[x] + 0.5f);
  if ((unsigned)yy >= (unsigned)SCR_H) return 0;
  return BRANCH_WOOD[yy * SCR_W + x];
}

static inline void blendBehind(int x, int y, uint16_t c, float a, const float* disp) {
  const int al = (int)(a * (255 - wood(x, y, disp)) * (32.0f / 255.0f) + 0.5f);
  if (al > 0) px_blend(x, y, c, al);
}

// Out there: a few pixels, covered per pixel by the branch. Coverage from four
// samples in each pixel.
static void drawFarLeaf(const Leaf& l, float alpha, int x0, int x1, const float* disp) {
  const LeafShape k = leafShape(l, 1.0f);
  if (l.x + k.half + 2 < x0 || l.x - k.half - 2 >= x1) return;
  const uint16_t col = rgb565(k.r, k.g, k.b);
  const float ih = 1.0f / (k.half * k.half), iw = 1.0f / (k.w * k.w);
  const int bx0 = (int)floorf(l.x - k.half - 1), bx1 = (int)ceilf(l.x + k.half + 1);
  const int by0 = (int)floorf(l.y - k.half - 1), by1 = (int)ceilf(l.y + k.half + 1);
  for (int Y = by0; Y < by1; Y++) {
    for (int X = bx0; X < bx1; X++) {
      int n = 0;
      for (int j = 0; j < 2; j++) {
        const float dy = (float)Y + 0.25f + 0.5f * j - l.y;
        for (int i = 0; i < 2; i++) {
          const float dx = (float)X + 0.25f + 0.5f * i - l.x;
          const float u = dx * k.c + dy * k.s, v = -dx * k.s + dy * k.c;
          n += u * u * ih + v * v * iw <= 1.0f;
        }
      }
      if (n) blendBehind(X, Y, col, alpha * n * 0.25f, disp);
    }
  }
}

static inline void splatBehind(float x, float y, uint16_t c, float a, const float* disp) {
  const int ix = (int)floorf(x), iy = (int)floorf(y);
  const float fx = x - ix, fy = y - iy;
  blendBehind(ix, iy, c, a * (1 - fx) * (1 - fy), disp);
  blendBehind(ix + 1, iy, c, a * fx * (1 - fy), disp);
  blendBehind(ix, iy + 1, c, a * (1 - fx) * fy, disp);
  blendBehind(ix + 1, iy + 1, c, a * fx * fy, disp);
}

void fallDrawBack(int x0, int x1, const float* disp) {
  for (int i = 0; i < MAX_LEAVES; i++) {
    const Leaf& l = s_leaves[i];
    if (l.on && !l.near && l.fade > 0) drawFarLeaf(l, 0.9f * l.fade, x0, x1, disp);
  }
  const int v = (int)(225.0f * skyLevel());
  const uint16_t c = rgb565(v, v, v > 8 ? v + 4 > 255 ? 255 : v + 4 : v);
  for (int i = 0; i < MAX_FAR_FLAKES; i++) {
    const FarFlake& f = s_far[i];
    if (!f.on || f.x < x0 - 2 || f.x >= x1 + 1) continue;
    splatBehind(f.x, f.y, c, 0.42f * f.fade, disp);
  }
}

// out of focus: the same leaf three times, wider and fainter outwards
void fallDrawFront(int x0, int x1) {
  for (int i = 0; i < MAX_LEAVES; i++) {
    const Leaf& l = s_leaves[i];
    if (!l.on || !l.near) continue;
    drawNearLeaf(l, 1.35f, 0.22f, x0, x1);
    drawNearLeaf(l, 1.0f, 0.40f, x0, x1);
    drawNearLeaf(l, 0.7f, 0.55f, x0, x1);
  }
}
