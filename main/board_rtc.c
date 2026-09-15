#include "board_rtc.h"

#include <stdio.h>
#include <string.h>

#include "board_i2c.h"
#include "esp_log.h"

// The calendar is seven consecutive BCD registers from 0x04.
#define REG_CONTROL1 0x00
#define REG_SECONDS 0x04
#define CAL_LEN 7
#define SECONDS_OS 0x80  // the oscillator stopped: the time is rubbish

static const char *TAG = "rtc";

static i2c_master_dev_handle_t s_dev;

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

// A stopped clock is seeded with the build time, so the scene at least starts
// in the right season until the network sets it.
static void build_time(rtc_time_t *t)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day = 1, year = 2026, hour = 12, minute = 0, second = 0;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    const char *p = strstr(months, mon);
    t->year = (uint16_t)year;
    t->month = p ? (uint8_t)((p - months) / 3 + 1) : 1;
    t->day = (uint8_t)day;
    t->hour = (uint8_t)hour;
    t->minute = (uint8_t)minute;
    t->second = (uint8_t)second;
    t->weekday = 0;
}

void board_rtc_init(void)
{
    if (i2c_bus_add(I2C_ADDR_RTC, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063A not on the bus at 0x%02x", I2C_ADDR_RTC);
        s_dev = NULL;
        return;
    }
    uint8_t sec = 0;
    if (i2c_reg_read(s_dev, REG_SECONDS, &sec, 1) != ESP_OK) {
        ESP_LOGE(TAG, "clock unreadable");
        s_dev = NULL;
        return;
    }
    if (sec & SECONDS_OS) {
        rtc_time_t t;
        build_time(&t);
        board_rtc_set(&t);
        ESP_LOGW(TAG, "clock had stopped, seeded with the build time %04u-%02u-%02u %02u:%02u", t.year,
                 t.month, t.day, t.hour, t.minute);
        return;
    }
    rtc_time_t now;
    if (board_rtc_get(&now)) {
        ESP_LOGI(TAG, "clock running: %04u-%02u-%02u %02u:%02u:%02u", now.year, now.month, now.day,
                 now.hour, now.minute, now.second);
    }
}

bool board_rtc_get(rtc_time_t *out)
{
    uint8_t buf[CAL_LEN];
    if (!s_dev || i2c_reg_read(s_dev, REG_SECONDS, buf, sizeof(buf)) != ESP_OK || (buf[0] & SECONDS_OS)) {
        return false;
    }
    out->second = from_bcd(buf[0] & 0x7F);
    out->minute = from_bcd(buf[1] & 0x7F);
    out->hour = from_bcd(buf[2] & 0x3F);
    out->day = from_bcd(buf[3] & 0x3F);
    out->weekday = buf[4] & 0x07;
    out->month = from_bcd(buf[5] & 0x1F);
    out->year = (uint16_t)(2000 + from_bcd(buf[6]));
    return true;
}

bool board_rtc_set(const rtc_time_t *t)
{
    // 24 hour mode, written every time so a wiped chip cannot come back in 12.
    if (!s_dev || i2c_reg_write(s_dev, REG_CONTROL1, 0x00) != ESP_OK) {
        return false;
    }
    const uint8_t buf[1 + CAL_LEN] = {
        REG_SECONDS,       to_bcd(t->second), to_bcd(t->minute), to_bcd(t->hour),
        to_bcd(t->day),    t->weekday,        to_bcd(t->month),  to_bcd((uint8_t)(t->year % 100)),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100) == ESP_OK;
}
