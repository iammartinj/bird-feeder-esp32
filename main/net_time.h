// The clock from the internet: joins the Wi-Fi network given over the console,
// asks an NTP server, writes local time into the RTC and switches the radio off
// again. Once at start and every twelve hours after. Without a network the RTC
// runs on its own.
//
// The network name, password and time zone live in NVS, never in the source.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void net_time_init(void);  // after nvs_flash_init

bool net_time_set_network(const char *ssid, const char *password);  // stores it and syncs
const char *net_time_ssid(void);                                   // "" when none

// POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" (the default) or "UTC0".
bool net_time_set_zone(const char *tz);
const char *net_time_zone(void);

void net_time_sync_now(void);
const char *net_time_status(void);

#ifdef __cplusplus
}
#endif
