#ifndef PIXAQ_FISH_ART_H
#define PIXAQ_FISH_ART_H
// ---------------------------------------------------------------------------
// fish_art.h -- in the aquarium this declared the fish sprites; the feeder only
// needs the pixel type they are made of, and keeps the name so the engine files
// stay as they are upstream.
// ---------------------------------------------------------------------------
#include <Arduino.h>

struct RGBA8 { uint8_t r, g, b, a; };

#endif // PIXAQ_FISH_ART_H
