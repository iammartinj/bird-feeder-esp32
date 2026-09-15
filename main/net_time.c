#include "net_time.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_rtc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"

#define NVS_NAMESPACE "feeder"
#define NVS_KEY_SSID "wifi_ssid"
#define NVS_KEY_PASS "wifi_pass"
#define NVS_KEY_TZ "tz"

#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define CONNECT_TIMEOUT_MS 30000
#define SNTP_TIMEOUT_MS 15000
#define RESYNC_MS (12UL * 3600 * 1000)
#define RETRIES 5

#define ONLINE_BIT BIT0

static const char *TAG = "net_time";

static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static char s_ssid[33];
static char s_pass[65];
static char s_tz[64] = DEFAULT_TZ;
static const char *s_status = "no network set";
static volatile bool s_connecting;
static int s_retries;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, ONLINE_BIT);
        if (s_connecting && s_retries < RETRIES) {
            s_retries++;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, ONLINE_BIT);
    }
}

static void load_settings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_ssid);
    if (nvs_get_str(h, NVS_KEY_SSID, s_ssid, &len) != ESP_OK) {
        s_ssid[0] = '\0';
    }
    len = sizeof(s_pass);
    if (nvs_get_str(h, NVS_KEY_PASS, s_pass, &len) != ESP_OK) {
        s_pass[0] = '\0';
    }
    len = sizeof(s_tz);
    if (nvs_get_str(h, NVS_KEY_TZ, s_tz, &len) != ESP_OK) {
        strlcpy(s_tz, DEFAULT_TZ, sizeof(s_tz));
    }
    nvs_close(h);
}

static void apply_zone(void)
{
    setenv("TZ", s_tz, 1);
    tzset();
}

// One round: join, ask, write the RTC, leave.
static void sync_once(void)
{
    if (!s_ssid[0]) {
        s_status = "no network set";
        return;
    }
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));

    s_status = "connecting";
    s_retries = 0;
    s_connecting = true;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (esp_wifi_start() != ESP_OK || esp_wifi_connect() != ESP_OK) {
        s_status = "radio would not start";
        s_connecting = false;
        esp_wifi_stop();
        return;
    }
    const EventBits_t bits =
        xEventGroupWaitBits(s_events, ONLINE_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (!(bits & ONLINE_BIT)) {
        ESP_LOGW(TAG, "could not join %s", s_ssid);
        s_status = "could not join the network";
    } else {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&sntp) == ESP_OK &&
            esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_TIMEOUT_MS)) == ESP_OK) {
            apply_zone();
            time_t now = 0;
            time(&now);
            struct tm lt;
            localtime_r(&now, &lt);
            const rtc_time_t t = {
                .year = (uint16_t)(lt.tm_year + 1900),
                .month = (uint8_t)(lt.tm_mon + 1),
                .day = (uint8_t)lt.tm_mday,
                .weekday = (uint8_t)lt.tm_wday,
                .hour = (uint8_t)lt.tm_hour,
                .minute = (uint8_t)lt.tm_min,
                .second = (uint8_t)lt.tm_sec,
            };
            if (board_rtc_set(&t)) {
                s_status = "clock set from the internet";
                ESP_LOGI(TAG, "clock set: %04u-%02u-%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour,
                         t.minute, t.second);
            }
        } else {
            ESP_LOGW(TAG, "no answer from the time server");
            s_status = "no answer from the time server";
        }
        esp_netif_sntp_deinit();
    }
    s_connecting = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    xEventGroupClearBits(s_events, ONLINE_BIT);
}

static void net_task(void *arg)
{
    for (;;) {
        sync_once();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RESYNC_MS));
    }
}

void net_time_init(void)
{
    load_settings();
    apply_zone();

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (!s_ssid[0]) {
        ESP_LOGW(TAG, "no network stored; set one with: WIFI <ssid> <password>");
    }
    xTaskCreate(net_task, "net_time", 4096, NULL, 3, &s_task);
}

static bool store(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_str(h, key, value) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool net_time_set_network(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= sizeof(s_ssid) || strlen(password) >= sizeof(s_pass)) {
        return false;
    }
    if (!store(NVS_KEY_SSID, ssid) || !store(NVS_KEY_PASS, password)) {
        return false;
    }
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, password, sizeof(s_pass));
    net_time_sync_now();
    return true;
}

const char *net_time_ssid(void)
{
    return s_ssid;
}

bool net_time_set_zone(const char *tz)
{
    if (!tz || !tz[0] || strlen(tz) >= sizeof(s_tz) || !store(NVS_KEY_TZ, tz)) {
        return false;
    }
    strlcpy(s_tz, tz, sizeof(s_tz));
    net_time_sync_now();
    return true;
}

const char *net_time_zone(void)
{
    return s_tz;
}

void net_time_sync_now(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

const char *net_time_status(void)
{
    return s_status;
}
