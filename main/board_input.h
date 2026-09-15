// What the scene hears from the board: a finger on the glass (CST820), the PWR
// key and whether a USB cable is in (AXP2101).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_init(void);
bool touch_down(void);  // a finger on the glass right now

void pmu_init(void);
bool pmu_usb(void);          // true also when the power chip does not answer
bool pmu_key_pressed(void);  // a short press of PWR since the last call

#ifdef __cplusplus
}
#endif
