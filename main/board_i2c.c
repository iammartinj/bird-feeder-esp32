#include "board_i2c.h"

#include "esp_log.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA GPIO_NUM_15
#define I2C_SCL GPIO_NUM_14
#define I2C_HZ 400000

static const char *TAG = "i2c";

static i2c_master_bus_handle_t s_bus;

void i2c_bus_init(void)
{
    if (s_bus) {
        return;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    ESP_LOGI(TAG, "bus up on SDA %d / SCL %d", I2C_SDA, I2C_SCL);
}

esp_err_t i2c_bus_add(uint8_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out);
}

esp_err_t i2c_reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 50);
}

esp_err_t i2c_reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t out[] = {reg, value};
    return i2c_master_transmit(dev, out, sizeof(out), 100);
}
