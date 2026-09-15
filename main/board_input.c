#include "board_input.h"

#include "board_i2c.h"
#include "esp_log.h"

static const char *TAG = "input";

// ------------------------------------------------------------------ touch --

// CST820: six bytes from 0x01 are gesture, finger count and both coordinates.
// Only whether a finger is down matters here.
#define TOUCH_REG_GESTURE 0x01
#define TOUCH_FRAME_LEN 6

static i2c_master_dev_handle_t s_touch;

void touch_init(void)
{
    if (i2c_bus_add(I2C_ADDR_TOUCH, &s_touch) != ESP_OK) {
        ESP_LOGE(TAG, "CST820 not on the bus at 0x%02x", I2C_ADDR_TOUCH);
        s_touch = NULL;
    }
}

bool touch_down(void)
{
    uint8_t buf[TOUCH_FRAME_LEN];
    if (!s_touch || i2c_reg_read(s_touch, TOUCH_REG_GESTURE, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    return (buf[1] & 0x0F) != 0;
}

// -------------------------------------------------------------------- pmu --

#define PMU_REG_STATUS1 0x00  // bit 5: VBUS good
#define PMU_REG_IRQ_ENABLE1 0x41
#define PMU_REG_IRQ_STATUS0 0x48
#define PMU_VBUS_GOOD 0x20

// Measured on this board: 0x02 key down, 0x08 key up, 0x01 short press (comes
// with the release). A long hold never arrives: the chip cuts the power itself.
#define IRQ_PKEY_SHORT 0x01
#define IRQ_PKEY_DOWN 0x02
#define IRQ_PKEY_UP 0x08

static i2c_master_dev_handle_t s_pmu;

void pmu_init(void)
{
    if (i2c_bus_add(I2C_ADDR_PMU, &s_pmu) != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 not on the bus at 0x%02x", I2C_ADDR_PMU);
        s_pmu = NULL;
        return;
    }
    uint8_t en = 0;
    if (i2c_reg_read(s_pmu, PMU_REG_IRQ_ENABLE1, &en, 1) == ESP_OK) {
        i2c_reg_write(s_pmu, PMU_REG_IRQ_ENABLE1, en | IRQ_PKEY_SHORT | IRQ_PKEY_DOWN | IRQ_PKEY_UP);
    }
    pmu_key_pressed();  // an old press must not be the first thing seen
}

bool pmu_usb(void)
{
    uint8_t s1 = 0;
    if (!s_pmu || i2c_reg_read(s_pmu, PMU_REG_STATUS1, &s1, 1) != ESP_OK) {
        return true;
    }
    return (s1 & PMU_VBUS_GOOD) != 0;
}

bool pmu_key_pressed(void)
{
    uint8_t st[3] = {0};
    if (!s_pmu || i2c_reg_read(s_pmu, PMU_REG_IRQ_STATUS0, st, sizeof(st)) != ESP_OK) {
        return false;
    }
    // A flag is cleared by writing a one back into it.
    for (int i = 0; i < 3; i++) {
        if (st[i]) {
            i2c_reg_write(s_pmu, (uint8_t)(PMU_REG_IRQ_STATUS0 + i), st[i]);
        }
    }
    return (st[1] & IRQ_PKEY_SHORT) != 0;
}
