#ifndef FEEDER_DRAW_H
#define FEEDER_DRAW_H
// ---------------------------------------------------------------------------
// draw.h -- the scene, back to front, a band of columns at a time:
//
//   background (lit)  ->  blackbird on the ground  ->  the feeder, turned about
//   its pivot  ->  shadows and birds on the feeder, turned with it  ->  birds on
//   the branch and in the air  ->  snow
//
// Sprites are drawn the aquarium's way (inverse transform per pixel) with
// bilinear sampling on premultiplied alpha, so they turn smoothly with the
// feeder.
// ---------------------------------------------------------------------------
#include "world.h"

void feederRenderBand(const World& w, int x0, int x1);

// How big a bird on the ground is drawn, against one on the feeder. The ground
// is metres behind the feeder and out of focus (the photo's depth of field), so
// the blackbird there is small and soft; nearer the bottom of the picture, a
// little bigger.
float groundScale(float y);

// Gives back the blurred copies of the ground bird's poses.
void feederDrawFree();

#endif
