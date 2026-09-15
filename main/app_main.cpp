// Bird feeder: a window onto a garden feeder, for the Waveshare
// ESP32-S3-Touch-AMOLED-1.8 (V2).
//
// Birds come, peck, squabble and leave; the feeder swings under them and in the
// wind; the light follows the time of day from the RTC; leaves fall in autumn,
// snow in winter. A tap on the glass scares the birds off and they come back
// one by one. The scene is main/scene on the engine in main/engine; this file
// is the loop around it.
//
// The scene is 320x240, drawn a band of columns at a time on both cores and
// stretched to the panel's 448x368 landscape (connector at the top).
#include <cstdio>
#include <cstring>

#include "draw.h"
#include "fall.h"
#include "fastmath.h"
#include "feeder_bg.h"
#include "gfx.h"
#include "sky.h"
#include "snow.h"
#include "stretch.h"
#include "world.h"

#include "background.h"
#include "board_display.h"
#include "board_i2c.h"
#include "board_input.h"
#include "board_rtc.h"
#include "console.h"
#include "net_time.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define BAND_COLS DISPLAY_BAND_ROWS  // screen columns a band: panel rows, the board being on its side
#define BANDS (DISP_W / BAND_COLS)
#define SCRATCH_COLS 48
#define TOUCH_POLL_MS 30
#define KEY_POLL_MS 100
#define CLOCK_MS 5000
#define STATS_MS 5000
#define HEAP_LOG_MS 60000
#define USB_CHECK_MS 5000
#define DARK_USB_MS (3LL * 3600 * 1000)       // the screen goes dark after this without a touch
#define DARK_BATTERY_MS (10LL * 60 * 1000)
#define TAP_MS 400
#define WIND_CALM 3.0f
#define WIND_MODERATE 9.0f

#define NVS_NAMESPACE "feeder"
#define NVS_KEY_BRIGHTNESS "brightness"

static const char* TAG = "feeder";

EXT_RAM_BSS_ATTR static World s_world;
static uint16_t* s_scratch;

// ---------------------------------------------------------------- drawing --
// Each band is split between the two cores: this task draws one half, a worker
// pinned to the other core the other.

typedef enum { JOB_RENDER, JOB_STRETCH } job_t;

static TaskHandle_t s_worker;
static SemaphoreHandle_t s_done;
static volatile job_t s_job;
static volatile int s_ja, s_jb;
static volatile int s_sx0, s_bx0;

static int64_t s_last, s_stats_at, s_heap_at, s_usb_at, s_clock_at, s_touch_at, s_key_at;
static int64_t s_touched_at, s_pressed_at;
static bool s_usb = true;
static bool s_down;
static uint32_t s_frames;
static int64_t s_acc_sim, s_acc_draw, s_acc_stretch, s_acc_push;

// Console requests, taken up by the drawing task.
static volatile float s_held_hour = -1.0f;  // < 0: the RTC's
static volatile bool s_clock_changed;
static volatile int s_request_visit = -1;
static volatile bool s_request_tap;
static volatile int s_request_fall = -1;        // a FallForce
static volatile int s_request_brightness = -1;  // percent
static volatile int s_request_screen = -1;      // 0 off, 1 on

static void run_job(job_t job, int a, int b)
{
    if (job == JOB_RENDER) {
        feederRenderBand(s_world, a, b);
    } else {
        stretchBand(s_scratch, s_sx0, display_band(), s_bx0, a, b);
    }
}

static void render_worker(void*)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_job(s_job, s_ja, s_jb);
        xSemaphoreGive(s_done);
    }
}

static void split(job_t job, int a, int b)
{
    if (!s_worker) {
        run_job(job, a, b);
        return;
    }
    const int m = a + (((b - a) / 2) & ~1);
    s_job = job;
    s_ja = a;
    s_jb = m;
    xTaskNotifyGive(s_worker);
    run_job(job, m, b);
    xSemaphoreTake(s_done, portMAX_DELAY);
}

static int count_birds(void)
{
    int n = 0;
    for (int i = 0; i < MAX_BIRDS; i++) {
        n += s_world.birds[i].state != BS_OFF;
    }
    return n;
}

static void frame(int64_t now)
{
    float dt = (float)(now - s_last) / 1000000.0f;
    s_last = now;
    if (dt > 1.0f / 30) dt = 1.0f / 30;

    worldStep(s_world, dt);
    skyStep(dt);
    snowStep(dt);
    fallStep(dt, s_world.windNow, s_world.coins.windy);
    const int64_t m1 = esp_timer_get_time();
    s_acc_sim += m1 - now;

    FB = s_scratch;
    for (int b = 0; b < BANDS; b++) {
        const int x0 = b * BAND_COLS;
        int sx0, sx1;
        stretchSource(x0, x0 + BAND_COLS, &sx0, &sx1);
        gBandX0 = sx0;
        gBandX1 = sx1;
        s_sx0 = sx0;
        s_bx0 = x0;
        const int64_t p0 = esp_timer_get_time();
        split(JOB_RENDER, sx0, sx1);
        const int64_t p1 = esp_timer_get_time();
        split(JOB_STRETCH, x0, x0 + BAND_COLS);
        const int64_t p2 = esp_timer_get_time();
        display_send_band(x0, BAND_COLS);
        s_acc_draw += p1 - p0;
        s_acc_stretch += p2 - p1;
        s_acc_push += esp_timer_get_time() - p2;
    }

    s_frames++;
    if (now - s_stats_at >= (int64_t)STATS_MS * 1000) {
        const float f = (float)(s_frames ? s_frames : 1);
        ESP_LOGI(TAG, "%.1f fps sim %.2f draw %.2f stretch %.2f panel %.2f ms, %d birds, %d visits, feeder %+.2f deg",
                 (double)(s_frames * 1e6f / (float)(now - s_stats_at)), (double)(s_acc_sim / 1000.0f / f),
                 (double)(s_acc_draw / 1000.0f / f), (double)(s_acc_stretch / 1000.0f / f),
                 (double)(s_acc_push / 1000.0f / f), count_birds(), s_world.visits,
                 (double)(s_world.theta * 57.2958f));
        s_frames = 0;
        s_acc_sim = s_acc_draw = s_acc_stretch = s_acc_push = 0;
        s_stats_at = now;
    }
}

// ---------------------------------------------------------------- control --

static float clock_hour(const rtc_time_t* t)
{
    return (float)t->hour + (float)t->minute / 60.0f + (float)t->second / 3600.0f;
}

static void save_brightness(uint8_t percent)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_BRIGHTNESS, percent);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void wake(int64_t now)
{
    display_set_on(true);
    s_touched_at = now;
    s_last = now;
}

static void control(int64_t now)
{
    if (s_clock_changed || now - s_clock_at > (int64_t)CLOCK_MS * 1000) {
        s_clock_changed = false;
        s_clock_at = now;
        const float held = s_held_hour;
        rtc_time_t t;
        if (held >= 0.0f) {
            worldSetClock(s_world, held);
            skySetClock(held);
        } else if (board_rtc_get(&t)) {
            const float h = clock_hour(&t);
            worldSetClock(s_world, h);
            skySetClock(h);
        }
    }
    const int visit = s_request_visit;
    if (visit >= 0) {
        s_request_visit = -1;
        ESP_LOGI(TAG, "console visit %s: %d came", speciesKey(visit), worldVisit(s_world, visit));
    }
    if (s_request_tap) {
        s_request_tap = false;
        worldStartle(s_world, "console");
    }
    const int fall = s_request_fall;
    if (fall >= 0) {
        s_request_fall = -1;
        fallForce((FallForce)fall);
    }
    const int brightness = s_request_brightness;
    if (brightness >= 0) {
        s_request_brightness = -1;
        display_set_brightness((uint8_t)brightness);
        save_brightness((uint8_t)brightness);
    }
    const int screen = s_request_screen;
    if (screen >= 0) {
        s_request_screen = -1;
        if (screen) {
            wake(now);
        } else {
            display_set_on(false);
        }
    }

    if (now - s_usb_at > (int64_t)USB_CHECK_MS * 1000) {
        s_usb_at = now;
        s_usb = pmu_usb();
    }

    // A tap is a press shorter than TAP_MS. On a dark screen it only wakes it.
    bool tapped = false;
    if (now - s_touch_at >= (int64_t)TOUCH_POLL_MS * 1000) {
        s_touch_at = now;
        const bool down = touch_down();
        if (down) {
            s_touched_at = now;
            if (!s_down) {
                s_pressed_at = now;
            }
        } else if (s_down && now - s_pressed_at <= (int64_t)TAP_MS * 1000) {
            tapped = true;
        }
        s_down = down;
    }
    if (tapped) {
        if (!display_is_on()) {
            wake(now);
        } else {
            worldStartle(s_world, "tap");
            ESP_LOGI(TAG, "tap on the glass");
        }
    }

    // The PWR key puts the screen out and back; held for six seconds the power
    // chip switches the board off by itself.
    if (now - s_key_at >= (int64_t)KEY_POLL_MS * 1000) {
        s_key_at = now;
        if (pmu_key_pressed()) {
            if (display_is_on()) {
                display_set_on(false);
            } else {
                wake(now);
            }
        }
    }

    const int64_t quiet_ms = (now - s_touched_at) / 1000;
    if (display_is_on() && quiet_ms > (s_usb ? DARK_USB_MS : DARK_BATTERY_MS)) {
        display_set_on(false);
        ESP_LOGI(TAG, "dark after %lld min without a touch, on %s", (long long)(quiet_ms / 60000),
                 s_usb ? "USB" : "battery");
    }

    if (now - s_heap_at > (int64_t)HEAP_LOG_MS * 1000) {
        s_heap_at = now;
        ESP_LOGI(TAG, "internal %u psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

static void scene_task(void*)
{
    for (;;) {
        const int64_t now = esp_timer_get_time();
        control(now);
        if (!display_is_on()) {
            // Nothing to draw and nothing moves: the garden waits for the screen.
            s_last = now;
            vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
            continue;
        }
        frame(now);
        vTaskDelay(1);
    }
}

// -------------------------------------------------------------------- open --

static bool open_scene(void)
{
    s_scratch = (uint16_t*)heap_caps_malloc((size_t)SCRATCH_COLS * FB_H * sizeof(uint16_t),
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_scratch) {
        ESP_LOGE(TAG, "no internal RAM for the scene band");
        return false;
    }
    fastMathInit();
    stretchInit();

    rtc_time_t t = {.year = 2026, .month = 1, .day = 1, .weekday = 0, .hour = 12, .minute = 0, .second = 0};
    board_rtc_get(&t);
    const float hour = clock_hour(&t);
    const uint32_t seed = esp_random();
    worldInit(s_world, seed, t.month, hour, -1);
    const Coins& c = s_world.coins;
    BGBUF = background_decode(FEEDER_BG[c.bg].data, FEEDER_BG[c.bg].len, FB_W, FB_H);
    if (!BGBUF) {
        return false;
    }
    skyInit(c.bg, s_world.winter, c.cloudPeriod, c.cloudDir);
    skySetClock(hour);
    snowInit(c.snow, c.windy ? WIND_MODERATE : WIND_CALM, seed ^ 0x5bd1e995u);
    fallInit(c.bg == BG_AUTUMN, seed ^ 0x9e3779b9u);
    char desc[200];
    worldDescribe(s_world, desc, sizeof(desc));
    ESP_LOGI(TAG, "%02d:%02d, month %d: %s", t.hour, t.minute, t.month, desc);

    s_done = xSemaphoreCreateBinary();
    TaskHandle_t worker = NULL;
    if (!s_done || xTaskCreatePinnedToCore(render_worker, "draw", 8192, NULL, 3, &worker, 0) != pdPASS) {
        ESP_LOGW(TAG, "no second drawing task; drawing on one core");
        worker = NULL;
    }
    s_worker = worker;

    const int64_t now = esp_timer_get_time();
    s_last = s_stats_at = s_heap_at = s_clock_at = s_touched_at = now;
    return true;
}

// ----------------------------------------------------------------- console --

bool app_command(const char* args, char* out, size_t out_len)
{
    float h = 0.0f;
    int n = 0;
    char key[16];
    if (sscanf(args, "BRIGHTNESS %d", &n) == 1 && n >= 0 && n <= 100) {
        s_request_brightness = n;
        snprintf(out, out_len, "OK brightness %d%%", n);
    } else if (strcmp(args, "SCREEN ON") == 0 || strcmp(args, "SCREEN OFF") == 0) {
        s_request_screen = strcmp(args + 7, "ON") == 0;
        snprintf(out, out_len, "OK screen %s", args + 7);
    } else if (sscanf(args, "HOUR %f", &h) == 1 && h >= 0.0f && h < 24.0f) {
        s_held_hour = h;
        s_clock_changed = true;
        snprintf(out, out_len, "OK clock held at %.2f h", (double)h);
    } else if (strcmp(args, "CLOCK") == 0) {
        s_held_hour = -1.0f;
        s_clock_changed = true;
        snprintf(out, out_len, "OK clock from the RTC");
    } else if (sscanf(args, "VISIT %15s", key) == 1) {
        const int sp = speciesByKey(key);
        if (sp < 0) {
            snprintf(out, out_len, "ERR no species %s (vrabec konadra modrinka zvonek stehlik brhlik kos "
                                   "hrdlicka strakapoud sojka veverka)", key);
        } else {
            s_request_visit = sp;
            snprintf(out, out_len, "OK visit %s", key);
        }
    } else if (strcmp(args, "TAP") == 0) {
        s_request_tap = true;
        snprintf(out, out_len, "OK tap");
    } else if (strcmp(args, "LEAF") == 0 || strcmp(args, "NEARLEAF") == 0 || strcmp(args, "FLURRY") == 0) {
        s_request_fall = args[0] == 'L' ? FALL_LEAVES : (args[0] == 'N' ? FALL_NEAR_LEAF : FALL_FLURRY);
        snprintf(out, out_len, "OK %s", args);
    } else {
        return false;
    }
    return true;
}

// -------------------------------------------------------------------- main --

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    i2c_bus_init();
    display_init();
    touch_init();
    pmu_init();
    board_rtc_init();

    uint8_t brightness = 100;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_BRIGHTNESS, &brightness);
        nvs_close(h);
    }
    display_set_brightness(brightness);

    console_init();
    net_time_init();

    if (!open_scene()) {
        ESP_LOGE(TAG, "the scene did not open");
        return;
    }
    xTaskCreatePinnedToCore(scene_task, "scene", 8192, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "running; internal %u psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
