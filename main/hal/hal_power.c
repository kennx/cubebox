#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include "hal/hal_power.h"

#define TAG "hal_power"

#define AXP2101_ADDR    0x34

/* AXP2101 registers */
#define AXP2101_LDO_ONOFF_CTRL0     0x90
#define AXP2101_LDO_ONOFF_CTRL1     0x91
#define AXP2101_DC_ONOFF_DVM_CTRL   0x80
#define AXP2101_DC_VOL0_CTRL        0x82
#define AXP2101_LDO_VOL0_CTRL       0x92
#define AXP2101_LDO_VOL1_CTRL       0x93
#define AXP2101_LDO_VOL2_CTRL       0x94
#define AXP2101_LDO_VOL3_CTRL       0x95

static i2c_master_dev_handle_t axp2101_dev = NULL;

static esp_err_t axp2101_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(axp2101_dev, buf, 2, pdMS_TO_TICKS(1000));
}

static esp_err_t axp2101_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(axp2101_dev, &reg, 1, val, 1, pdMS_TO_TICKS(1000));
}

static void axp2101_set_bit(uint8_t reg, uint8_t bit)
{
    uint8_t val;
    if (axp2101_read_reg(reg, &val) == ESP_OK) {
        val |= (1 << bit);
        axp2101_write_reg(reg, val);
    }
}

static void axp2101_clr_bit(uint8_t reg, uint8_t bit)
{
    uint8_t val;
    if (axp2101_read_reg(reg, &val) == ESP_OK) {
        val &= ~(1 << bit);
        axp2101_write_reg(reg, val);
    }
}

void hal_power_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &axp2101_dev));

    /* DC1 = 3300mV, value = (3300 - 1500) / 100 = 18 */
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_DC_VOL0_CTRL, 18));
    /* ALDO1~4 = 3300mV, value = (3300 - 500) / 100 = 28 */
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL0_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL1_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL2_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL3_CTRL, 28));

    /* Enable DC1 (bit 0) and ALDO1/2/4 (bits 0/1/3). ALDO3 is toggled by LCD reset. */
    axp2101_set_bit(AXP2101_DC_ONOFF_DVM_CTRL, 0);
    uint8_t ldo_onoff;
    ESP_ERROR_CHECK(axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &ldo_onoff));
    ldo_onoff |= 0x0B;
    ldo_onoff &= ~0xB4;
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_ONOFF_CTRL0, ldo_onoff));

    uint8_t ldo_onoff1;
    ESP_ERROR_CHECK(axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL1, &ldo_onoff1));
    ldo_onoff1 &= ~0x03;
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_ONOFF_CTRL1, ldo_onoff1));

    ESP_LOGI(TAG, "PMIC initialized");
}

void hal_power_lcd_reset(void)
{
    /* High-Low-High reset sequence (match official Waveshare code) */
    axp2101_set_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Power on */
    vTaskDelay(pdMS_TO_TICKS(100));
    axp2101_clr_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Reset */
    vTaskDelay(pdMS_TO_TICKS(100));
    axp2101_set_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Release reset */
    vTaskDelay(pdMS_TO_TICKS(100));
}
