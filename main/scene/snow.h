#ifndef FEEDER_SNOW_H
#define FEEDER_SNOW_H
// ---------------------------------------------------------------------------
// snow.h -- falling snow in front of everything, in the winter scenes.
//
// The aquarium's bubbles turned upside down: small soft points of light,
// slower, white, drifting sideways on the wind. How much falls is a coin at
// start (none, light, heavy) and does not change while the scene runs.
// ---------------------------------------------------------------------------
#include <Arduino.h>

void snowInit(int density, float wind, uint32_t seed);   // density 0, 1, 2
void snowStep(float dt);
void snowDraw(int x0, int x1);                           // columns [x0, x1)

#endif
