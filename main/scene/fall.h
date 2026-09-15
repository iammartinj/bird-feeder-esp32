#ifndef FEEDER_FALL_H
#define FEEDER_FALL_H
// ---------------------------------------------------------------------------
// fall.h -- things now and then falling in the garden behind the feeder.
//
//   autumn   a leaf or two every minute or two, more of them in a gust; small
//            and hazy out there, tumbling as they come down and lying a moment
//            on the ground before they are lost among the others. One leaf in
//            five comes by close in front instead: big, fast, out of focus.
//   winter   a far flurry of fine snow every few minutes, twenty seconds to a
//            minute, thicker in the gusts, behind the feeder (the snow in front
//            is snow.h and stays as the coin says).
//
// Leaves are drawn, not art: a tumbling ellipse in the colours of the leaves on
// the autumn photo's ground. Both are pushed by the same wind as the feeder and
// the branch (world.h).
// ---------------------------------------------------------------------------
#include <Arduino.h>

enum FallForce : uint8_t { FALL_LEAVES, FALL_NEAR_LEAF, FALL_FLURRY };

void fallInit(bool autumn, uint32_t seed);   // otherwise winter
void fallStep(float dt, float wind, bool windy);   // wind: world.windNow, about -2..2
// Right after the photo, behind everything; the branch in the photo is nearer
// and hides what falls behind it (BRANCH_WOOD, moved down by disp[x] as sky.h
// bends it).
void fallDrawBack(int x0, int x1, const float* disp);
void fallDrawFront(int x0, int x1);          // in front of everything but the snow
void fallForce(FallForce what);              // now, whatever the clock says (tests)

#endif
