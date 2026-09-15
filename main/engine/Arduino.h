#ifndef AQ_ARDUINO_H
#define AQ_ARDUINO_H
// ---------------------------------------------------------------------------
// Arduino.h -- the handful of Arduino names the upstream sources use, on
// ESP-IDF (and on the host preview), so that those sources keep their includes.
//
//   esp_random, IRAM_ATTR, xPortGetCoreID   ESP-IDF's own; stubbed on the host
//   PROGMEM                                 nothing: flash is addressable
//   Serial.printf / println                 ESP_LOGI with the same text
//   EXT_RAM_BSS_ATTR                        big static arrays go to PSRAM,
//                                           internal RAM is kept for DMA and stacks
// ---------------------------------------------------------------------------
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

#define PROGMEM

#ifdef AQ_HOST
#define IRAM_ATTR
#define EXT_RAM_BSS_ATTR
static inline int xPortGetCoreID(void) { return 0; }
uint32_t esp_random(void);
#else
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

struct AqSerial {
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit(buf);
  }
  void println(const char* s) { emit(s); }

 private:
  static void emit(const char* s) {
    // The upstream lines end in a newline; the log adds its own.
    char buf[200];
    size_t n = strnlen(s, sizeof(buf) - 1);
    memcpy(buf, s, n);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    const char* p = buf;
    while (*p == '\n') p++;
#ifdef AQ_HOST
    fprintf(stderr, "aquarium: %s\n", p);
#else
    ESP_LOGI("aquarium", "%s", p);
#endif
  }
};
static AqSerial Serial;

#endif  // AQ_ARDUINO_H
