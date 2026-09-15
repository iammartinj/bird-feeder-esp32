// The 1.8" AMOLED: a CO5300 on QSPI, 368 x 448 portrait.
//
// The scene is sent in bands of panel rows from one buffer in internal DMA
// memory: a whole frame would have to live in PSRAM, where the processor and the
// DMA transfer fight over the same bus. Each band is waited for, so the next
// one can be put together in the same buffer.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_W 368
#define PANEL_H 448
#define DISPLAY_BAND_ROWS 56

void display_init(void);  // after i2c_bus_init

// PANEL_W x DISPLAY_BAND_ROWS pixels, RGB565 in memory order.
uint16_t *display_band(void);

// Sends the first `rows` rows of the band to panel rows [y, y + rows) and waits
// for the transfer to finish. The buffer is byte-swapped on the way out.
void display_send_band(int y, int rows);

void display_set_on(bool on);
bool display_is_on(void);

// Only between bands: the command goes out on the same bus.
void display_set_brightness(uint8_t percent);
uint8_t display_brightness(void);

#ifdef __cplusplus
}
#endif
