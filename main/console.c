#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_rtc.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_time.h"

#define CONSOLE_LINE_MAX 200

static const char *TAG = "console";

static void reply(const char *fmt, ...)
{
    char line[CONSOLE_LINE_MAX + 2];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, CONSOLE_LINE_MAX, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    n = n > CONSOLE_LINE_MAX - 1 ? CONSOLE_LINE_MAX - 1 : n;
    line[n] = '\n';
    usb_serial_jtag_write_bytes((const uint8_t *)line, (size_t)n + 1, pdMS_TO_TICKS(100));
}

static const char *HELP =
    "commands: WIFI <ssid> <password> | WIFI? | TZ <posix tz> | TZ? | SYNC | TIME | "
    "BRIGHTNESS <0-100> | SCREEN ON|OFF | HOUR <0-24> | CLOCK | VISIT <species> | TAP | "
    "LEAF | NEARLEAF | FLURRY";

static void handle(char *line)
{
    char answer[160];
    if (strncmp(line, "WIFI ", 5) == 0) {
        // The password is everything after the first space following the name;
        // it may itself contain spaces, and it is never echoed or logged.
        char *ssid = line + 5;
        char *pass = strchr(ssid, ' ');
        if (pass) {
            *pass++ = '\0';
        }
        if (net_time_set_network(ssid, pass ? pass : "")) {
            reply("OK network %s stored, syncing the clock", ssid);
        } else {
            reply("ERR could not store the network");
        }
    } else if (strcmp(line, "WIFI?") == 0) {
        reply("network: %s; %s", net_time_ssid()[0] ? net_time_ssid() : "(none)", net_time_status());
    } else if (strncmp(line, "TZ ", 3) == 0) {
        reply(net_time_set_zone(line + 3) ? "OK time zone %s" : "ERR bad time zone %s", line + 3);
    } else if (strcmp(line, "TZ?") == 0) {
        reply("time zone: %s", net_time_zone());
    } else if (strcmp(line, "SYNC") == 0) {
        net_time_sync_now();
        reply("OK syncing");
    } else if (strcmp(line, "TIME") == 0) {
        rtc_time_t t;
        if (board_rtc_get(&t)) {
            reply("%04u-%02u-%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute, t.second);
        } else {
            reply("ERR clock not set");
        }
    } else if (strcmp(line, "HELP") == 0) {
        reply("%s", HELP);
    } else if (app_command(line, answer, sizeof(answer))) {
        reply("%s", answer);
    } else if (line[0]) {
        reply("%s", HELP);
    }
}

static void console_task(void *arg)
{
    char line[CONSOLE_LINE_MAX];
    size_t len = 0;
    uint8_t c;
    for (;;) {
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1 || c == '\r') {
            continue;
        }
        if (c == '\n') {
            line[len] = '\0';
            handle(line);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = (char)c;
        }
    }
}

void console_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "console unavailable");
        return;
    }
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "console ready, type HELP");
}
