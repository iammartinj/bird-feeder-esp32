#ifndef AQ_STRETCH_H
#define AQ_STRETCH_H
// ---------------------------------------------------------------------------
// stretch.h -- the scene, drawn at the upstream's 320x240, stretched bilinearly
// to the board's whole 448x368 landscape.
//
// Both sides are laid out as bands of columns, the way the panel on its side
// takes them: pixel (x, y) of a band starting at column x0 is at
// (x - x0) * height + (height - 1 - y). RGB565 in memory order.
// ---------------------------------------------------------------------------
#include <stdint.h>

static const int DISP_W = 448;
static const int DISP_H = 368;

void stretchInit();

// The scene columns a band of screen columns [dx0, dx1) reads, as an even
// range, so that the lighting's pixel pairs line up.
void stretchSource(int dx0, int dx1, int* sx0, int* sx1);

// Screen columns [dx0, dx1) of the band starting at screen column bx0, from the
// scene band `src` starting at scene column sx0.
void stretchBand(const uint16_t* src, int sx0, uint16_t* dst, int bx0, int dx0, int dx1);

#endif  // AQ_STRETCH_H
