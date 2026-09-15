#ifndef PIXAQ_GFX_H
#define PIXAQ_GFX_H
// ---------------------------------------------------------------------------
// gfx.h — RGB565 framebuffer primitives.
//
// The browser build composites everything through canvas 2D with float alpha.
// Here the scene is composed into one RGB565 buffer with 5-bit alpha blends
// (the classic split-field trick), then pushed to the panel in one SPI burst.
//
// On the Waveshare 1.8" AMOLED: the board is a 368x448 portrait panel laid on
// its side, and there is no room in internal RAM for a whole frame. So:
//
//   - the simulation keeps the upstream's 320x240 space (SIM_W x SIM_H). The
//     scene is drawn at SCR_W x SCR_H; positions scale by SX / SY and shapes by
//     SCALE. Drawn straight at the board's 448x368 it ran at 11 fps, so it is
//     drawn at the upstream's own size (all three are 1) and the firmware
//     stretches each band to the screen (stretch.h);
//   - a band is a run of scene *columns*, which is a run of panel rows once the
//     board is on its side, and FB points at the band being composed. Scene
//     pixel (x, y) sits at column x - gBandX0, row FB_H - 1 - y; fb_px() does
//     that;
//   - the clip is on x, for the same reason.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <math.h>

// --- tank framebuffer geometry ---------------------------------------------
// The whole panel is the tank: a photo backdrop with the fish composited over
// it, so the framebuffer covers all of it.
static const int SIM_W = 320;
static const int SIM_H = 240;
static const int SCR_W = 320;
static const int SCR_H = 240;
static const int FB_W = SCR_W;
static const int FB_H = SCR_H;
static const int OFF_Y = 0;

static const float SX = (float)SCR_W / SIM_W;
static const float SY = (float)SCR_H / SIM_H;
static const float SCALE = SX;                      // shapes, both ways
static const float INV_SX = 1.0f / SX;
static const float INV_SY = 1.0f / SY;
static const float INV_SCALE = 1.0f / SCALE;

extern uint16_t* FB;      // the band being composed: columns gBandX0.. as panel rows
extern uint16_t* BGBUF;   // decoded tank photo, PSRAM, laid out like FB for the whole scene
extern int gBandX0, gBandX1;

static inline uint16_t* fb_px(int x, int y) {
  return FB + (x - gBandX0) * FB_H + (FB_H - 1 - y);
}
static inline bool fb_in(int x, int y) {
  return (unsigned)(x - gBandX0) < (unsigned)(gBandX1 - gBandX0) && (unsigned)y < (unsigned)FB_H;
}

// --- dirty box ---------------------------------------------------------------
// Every write goes through px_blend()/px_add(), so accumulating the touched box
// there is exact: the box a fish reports always contains every pixel it wrote.
// That is what lets the next frame restore only those rows from the backdrop
// and push only those rows to the panel.
extern int gDX0, gDY0, gDX1, gDY1;

// Column band currently being composed, per core. The frame is rendered and sent
// in bands, and each band is split again between the two cores - so the clip is
// the one piece of drawing state that cannot be global. Clamping loop bounds to
// it costs nothing per pixel; it is only ever read when a shape sets up its span
// loops.
extern int gClipX0[2], gClipX1[2];
static inline void clipBand(int x0, int x1) {
  const int c = xPortGetCoreID();
  gClipX0[c] = x0;
  gClipX1[c] = x1;
}

static inline void dirtyReset() { gDX0 = FB_W; gDY0 = FB_H; gDX1 = 0; gDY1 = 0; }
static inline void dirtyMark(int x, int y) {
  if (x < gDX0) gDX0 = x;
  if (x >= gDX1) gDX1 = x + 1;
  if (y < gDY0) gDY0 = y;
  if (y >= gDY1) gDY1 = y + 1;
}

static constexpr uint16_t rgb565(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// alpha 0..32
static inline uint16_t blend565(uint16_t dst, uint16_t src, uint32_t a) {
  if (a >= 32) return src;
  if (a == 0) return dst;
  uint32_t ia = 32 - a;
  uint32_t drb = dst & 0xF81F, dg = dst & 0x07E0;
  uint32_t srb = src & 0xF81F, sg = src & 0x07E0;
  uint32_t orb = ((srb * a + drb * ia) >> 5) & 0xF81F;
  uint32_t og  = ((sg  * a + dg  * ia) >> 5) & 0x07E0;
  return (uint16_t)(orb | og);
}

static inline uint16_t add565(uint16_t dst, int r, int g, int b) {
  int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
  dr += r; dg += g; db += b;
  if (dr > 31) dr = 31;
  if (dg > 63) dg = 63;
  if (db > 31) db = 31;
  return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static inline void px_blend(int x, int y, uint16_t c, int a) {
  if (!fb_in(x, y)) return;
  if (a <= 0) return;
  uint16_t* p = fb_px(x, y);
  *p = blend565(*p, c, a > 32 ? 32 : (uint32_t)a);
  dirtyMark(x, y);
}

// Premultiplied source-over. The strip renderer accumulates colour already
// multiplied by coverage, so blending it directly avoids un-premultiplying -
// three float divides per pixel that cost more than everything around them.
// pr/pg/pb are 5/6/5-bit premultiplied, a is 0..32.
static inline void px_blend_pm(int x, int y, int pr, int pg, int pb, int a) {
  if (!fb_in(x, y)) return;
  uint16_t* p = fb_px(x, y);
  uint16_t d = *p;
  int ia = 32 - a;
  int r = (((d >> 11) & 0x1F) * ia >> 5) + pr;
  int g = (((d >> 5) & 0x3F) * ia >> 5) + pg;
  int b = ((d & 0x1F) * ia >> 5) + pb;
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  *p = (uint16_t)((r << 11) | (g << 5) | b);
  dirtyMark(x, y);
}

static inline void px_add(int x, int y, int r, int g, int b) {
  if (!fb_in(x, y)) return;
  uint16_t* p = fb_px(x, y);
  *p = add565(*p, r, g, b);
  dirtyMark(x, y);
}

static inline void fb_hline(int x0, int x1, int y, uint16_t c) {
  if ((unsigned)y >= (unsigned)FB_H) return;
  if (x0 < gBandX0) x0 = gBandX0;
  if (x1 > gBandX1) x1 = gBandX1;
  for (int x = x0; x < x1; x++) *fb_px(x, y) = c;
}

static inline void fb_rect(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++) fb_hline(x, x + w, j, c);
}

static inline void fb_rect_a(int x, int y, int w, int h, uint16_t c, int a) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) px_blend(i, j, c, a);
}

// --- anti-aliased primitives (3 sub-scanlines + exact span coverage) -------
// These take simulation coordinates, like everything the renderer hands them,
// and scale to pixels themselves.
struct PolyPaint {
  bool  grad = false;
  bool  additive = false;
  float gx0 = 0, gy0 = 0, gx1 = 0, gy1 = 0;
  uint8_t r0 = 255, g0 = 255, b0 = 255; float a0 = 1.0f;
  uint8_t r1 = 255, g1 = 255, b1 = 255; float a1 = 1.0f;
};

void fillPolyAA(const float* xs, const float* ys, int n, const PolyPaint& p);
void fillRectAA(float x, float y, float w, float h, uint16_t c, float a);
void lineAA(float x0, float y0, float x1, float y1, float w, uint16_t c, float a);
void circleOutlineAA(float cx, float cy, float r, float lw, uint16_t c, float a);

// (fbSwapBand is gone: the display driver swaps the band on the way out.)

// tiny 3x5 label font - only the glyphs the depth rail needs (0-9, c, m)
void fb_label(int x, int y, const char* s, uint16_t c, float a);

#endif // PIXAQ_GFX_H
