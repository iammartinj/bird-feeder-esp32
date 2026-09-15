// The board's one I2C bus: touch, IO expander, power chip and RTC share it.
// Opened once; every driver takes a device handle from here.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_ADDR_TOUCH 0x15     // CST820
#define I2C_ADDR_EXPANDER 0x20  // TCA9554: panel reset and power, touch reset
#define I2C_ADDR_PMU 0x34       // AXP2101
#define I2C_ADDR_RTC 0x51       // PCF85063A

void i2c_bus_init(void);
esp_err_t i2c_bus_add(uint8_t addr, i2c_master_dev_handle_t *out);
esp_err_t i2c_reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len);
esp_err_t i2c_reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif
