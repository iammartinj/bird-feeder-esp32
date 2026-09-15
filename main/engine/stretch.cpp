#pragma GCC optimize("O3")
#include "stretch.h"
#include "gfx.h"

// Where each screen column and row samples the scene: the lower neighbour and a
// weight 0..16 towards the upper one. Pixel centres line up, as a resize should.
static uint16_t colIdx[DISP_W], rowIdx[DISP_H];
static uint8_t colW[DISP_W], rowW[DISP_H];

static void sampling(int dst, int src, uint16_t* idx, uint8_t* w) {
  for (int i = 0; i < dst; i++) {
    float u = ((float)i + 0.5f) * (float)src / (float)dst - 0.5f;
    if (u < 0) u = 0;
    int k = (int)u;
    if (k > src - 2) k = src - 2;
    int f = (int)((u - (float)k) * 16.0f + 0.5f);
    if (f > 16) f = 16;
    idx[i] = (uint16_t)k;
    w[i] = (uint8_t)f;
  }
}

void stretchInit() {
  sampling(DISP_W, SCR_W, colIdx, colW);
  sampling(DISP_H, SCR_H, rowIdx, rowW);
}

void stretchSource(int dx0, int dx1, int* sx0, int* sx1) {
  int a = colIdx[dx0] & ~1;
  int b = (colIdx[dx1 - 1] + 2 + 1) & ~1;
  if (b > SCR_W) b = SCR_W;
  *sx0 = a;
  *sx1 = b;
}

static inline uint16_t mix565(uint16_t a, uint16_t b, int w) {
  const int iw = 16 - w;
  const int r = (((a >> 11) & 0x1F) * iw + ((b >> 11) & 0x1F) * w) >> 4;
  const int g = (((a >> 5) & 0x3F) * iw + ((b >> 5) & 0x3F) * w) >> 4;
  const int bl = ((a & 0x1F) * iw + (b & 0x1F) * w) >> 4;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

#ifdef AQ_STRETCH_BILINEAR
// 46 ms a frame on the board, as long as drawing the scene itself.
void IRAM_ATTR stretchBand(const uint16_t* src, int sx0, uint16_t* dst, int bx0, int dx0, int dx1) {
  for (int dx = dx0; dx < dx1; dx++) {
    const int wx = colW[dx];
    const uint16_t* c0 = src + (colIdx[dx] - sx0) * SCR_H;
    const uint16_t* c1 = c0 + SCR_H;
    uint16_t* out = dst + (dx - bx0) * DISP_H;
    for (int dy = 0; dy < DISP_H; dy++) {
      // scene row y lives at SCR_H - 1 - y, so its lower neighbour is one before
      const int k = SCR_H - 1 - rowIdx[dy];
      const int wy = rowW[dy];
      const uint16_t top = wx ? mix565(c0[k], c1[k], wx) : c0[k];
      const uint16_t bot = wx ? mix565(c0[k - 1], c1[k - 1], wx) : c0[k - 1];
      out[DISP_H - 1 - dy] = wy ? mix565(top, bot, wy) : top;
    }
  }
}
#else
// Nearest pixel: each screen pixel takes the scene pixel its centre falls in.
static uint16_t nearRow[DISP_H];   // screen row slot -> scene row slot, both flipped

void IRAM_ATTR stretchBand(const uint16_t* src, int sx0, uint16_t* dst, int bx0, int dx0, int dx1) {
  if (!nearRow[0] && !nearRow[DISP_H - 1]) {
    for (int dy = 0; dy < DISP_H; dy++) {
      const int sy = (dy * SCR_H + SCR_H / 2) / DISP_H;
      nearRow[DISP_H - 1 - dy] = (uint16_t)(SCR_H - 1 - (sy < SCR_H ? sy : SCR_H - 1));
    }
  }
  for (int dx = dx0; dx < dx1; dx++) {
    const int sx = (dx * SCR_W + SCR_W / 2) / DISP_W;
    const uint16_t* col = src + (sx - sx0) * SCR_H;
    uint16_t* out = dst + (dx - bx0) * DISP_H;
    for (int i = 0; i < DISP_H; i++) out[i] = col[nearRow[i]];
  }
}
#endif
