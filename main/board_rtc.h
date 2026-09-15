// PCF85063A real-time clock. It keeps local time; the scene's light and the
// birds follow it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} rtc_time_t;

void board_rtc_init(void);
bool board_rtc_get(rtc_time_t *out);  // false when the clock stopped and nobody set it
bool board_rtc_set(const rtc_time_t *t);

#ifdef __cplusplus
}
#endif
