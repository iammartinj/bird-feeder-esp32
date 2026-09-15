#include "board_display.h"

#include "board_i2c.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_HOST SPI2_HOST
#define LCD_BPP 16

// The driver's own macro sets 40 MHz; the panel runs at 80 on this board.
#define LCD_PCLK_HZ (80 * 1000 * 1000)

#define LCD_CS GPIO_NUM_12
#define LCD_PCLK GPIO_NUM_11
#define LCD_D0 GPIO_NUM_4
#define LCD_D1 GPIO_NUM_5
#define LCD_D2 GPIO_NUM_6
#define LCD_D3 GPIO_NUM_7

// The V2 board's panel sits 16 columns in.
#define X_GAP 0x10

// TCA9554 IO expander: panel reset, panel power and touch reset hang off it.
#define EXP_REG_OUTPUT 0x01
#define EXP_REG_CONFIG 0x03
#define EXP_LCD_RST BIT(0)
#define EXP_PWR_EN BIT(1)
#define EXP_TOUCH_RST BIT(2)
#define EXP_SD_CS BIT(7)
#define EXP_OUTPUTS (EXP_LCD_RST | EXP_PWR_EN | EXP_TOUCH_RST | EXP_SD_CS)

#define BAND_TIMEOUT_MS 500

// QSPI commands go out shifted under a write opcode.
#define QSPI_CMD(c) ((0x02 << 24) | ((c) << 8))
#define CMD_BRIGHTNESS 0x51
#define CMD_HBM_BRIGHTNESS 0x63
#define CMD_CTRL_DISPLAY 0x53

static const char *TAG = "display";

static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 0},
};

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_band_done;
static uint16_t *s_band;
static bool s_on = true;
static uint8_t s_brightness = 100;

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event,
                                    void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_band_done, &woken);
    return woken == pdTRUE;
}

// Hold panel and touch in reset, drop panel power, bring it all back. Two
// pulses: one short pulse has been seen to leave the panel dark. This is also
// what resets the touch controller.
static void power_cycle_panel(void)
{
    i2c_master_dev_handle_t exp = NULL;
    if (i2c_bus_add(I2C_ADDR_EXPANDER, &exp) != ESP_OK) {
        ESP_LOGW(TAG, "IO expander 0x%02x not reachable", I2C_ADDR_EXPANDER);
        return;
    }
    // After a warm reset the expander has been seen to miss the first write.
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = i2c_reg_write(exp, EXP_REG_CONFIG, (uint8_t)~EXP_OUTPUTS);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IO expander did not accept config, panel may stay dark");
        i2c_master_bus_rm_device(exp);
        return;
    }
    for (int i = 0; i < 2; i++) {
        i2c_reg_write(exp, EXP_REG_OUTPUT, EXP_SD_CS);    // resets low, panel power off
        vTaskDelay(pdMS_TO_TICKS(50));
        i2c_reg_write(exp, EXP_REG_OUTPUT, EXP_OUTPUTS);  // released, power on
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    i2c_master_bus_rm_device(exp);
}

void display_init(void)
{
    power_cycle_panel();

    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(LCD_PCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3,
                                                              PANEL_W * DISPLAY_BAND_ROWS * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    const co5300_vendor_config_t vendor = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,  // reset hangs off the expander
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config = (void *)&vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, X_GAP, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_band_done = xSemaphoreCreateBinary();
    s_band = heap_caps_malloc((size_t)PANEL_W * DISPLAY_BAND_ROWS * sizeof(uint16_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_band_done && s_band);
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_color_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL));
    ESP_LOGI(TAG, "panel up, %d x %d, QSPI %d MHz", PANEL_W, PANEL_H, LCD_PCLK_HZ / 1000000);
}

uint16_t *display_band(void)
{
    return s_band;
}

void display_send_band(int y, int rows)
{
    if (!s_on || rows <= 0 || rows > DISPLAY_BAND_ROWS) {
        return;
    }
    // The panel wants RGB565 the other way round from memory order.
    const size_t px = (size_t)PANEL_W * (size_t)rows;
    for (size_t i = 0; i < px; i++) {
        s_band[i] = (uint16_t)((s_band[i] >> 8) | (s_band[i] << 8));
    }
    xSemaphoreTake(s_band_done, 0);
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_W, y + rows, s_band) != ESP_OK) {
        return;
    }
    if (xSemaphoreTake(s_band_done, pdMS_TO_TICKS(BAND_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "band at row %d never finished", y);
    }
}

void display_set_on(bool on)
{
    if (on == s_on) {
        return;
    }
    if (esp_lcd_panel_disp_on_off(s_panel, on) == ESP_OK) {
        s_on = on;
        ESP_LOGI(TAG, "screen %s", on ? "on" : "off");
    }
}

bool display_is_on(void)
{
    return s_on;
}

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    const uint8_t level = (uint8_t)((percent * 255) / 100);
    // Both registers: while the panel is in high brightness mode 0x63 is the one
    // that counts, and 0x51 alone changes nothing. 0x53 bit 5 enables the block.
    esp_lcd_panel_io_tx_param(s_io, QSPI_CMD(CMD_BRIGHTNESS), (uint8_t[]){level}, 1);
    esp_lcd_panel_io_tx_param(s_io, QSPI_CMD(CMD_HBM_BRIGHTNESS), (uint8_t[]){level}, 1);
    esp_lcd_panel_io_tx_param(s_io, QSPI_CMD(CMD_CTRL_DISPLAY), (uint8_t[]){0x28}, 1);
    s_brightness = percent;
}

uint8_t display_brightness(void)
{
    return s_brightness;
}
