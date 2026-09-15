#ifndef FEEDER_SKY_H
#define FEEDER_SKY_H
// ---------------------------------------------------------------------------
// sky.h -- the garden's light, laid over the background photo every frame.
//
// The pass is the aquarium's (BGBUF -> FB, a band of columns at a time); what
// it computes is new:
//
//   time of day  brightness and colour from the clock: warm in the morning,
//                flat at noon, golden towards evening, 12 % and blue-grey at
//                night, with 40-minute transitions instead of steps
//   clouds       a broad slow wave of brightness across the picture, +-8 %
//   sun          on the sunny morning photo, a brighter band wandering across,
//                as when the sun comes and goes between clouds
//
// Caustics and the LED flicker are not used; the clouds and the snow are what
// keep the photograph from standing still. All of it is constant down a column,
// so it costs one gain per column.
// ---------------------------------------------------------------------------
#include <Arduino.h>

void skyInit(int background, bool winter, float cloudPeriodS, float cloudDir);
void skySetClock(float hour);       // local time as hours with a fraction
void skyStep(float dt);
// BGBUF -> FB, lit, columns [x0, x1). Where the branch is (BRANCH_MASK) the photo
// is shifted down by disp[x] pixels, sub-pixel, fading out over the mask's soft
// edge, so the branch sways without a cut-out.
void skyApply(int x0, int x1, const float* disp);

// What a sprite at column x gets: the full time-of-day colour, and 30 % of the
// clouds and sun at that spot (the aquarium's FISH_LIGHT_MIX).
void skySpriteGain(float x, float rgb[3]);

float skyLevel();                   // 0.12 at night .. 1 by day
bool skyNight();

#endif
