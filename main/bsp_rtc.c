#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include "bsp_rtc.h"

#define TAG "bsp_rtc"

#define PCF85063_ADDR   0x51  /* RTC I2C address */

static i2c_master_dev_handle_t pcf85063_dev = NULL;

const char *const bsp_rtc_weekdays_str[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const bsp_rtc_months_str[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static esp_err_t rtc_pcf85063_write_reg(uint8_t reg, uint8_t val)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pcf85063_dev, buf, 2, pdMS_TO_TICKS(1000));
}

static inline uint8_t bcd2dec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

static inline uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

void bsp_rtc_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &pcf85063_dev));
    ESP_LOGI(TAG, "PCF85063 RTC device added to I2C bus");

    /* Force 24H mode by clearing bit 2 in Control_1 (0x00) */
    ESP_ERROR_CHECK(rtc_pcf85063_write_reg(0x00, 0x00));
    ESP_LOGI(TAG, "PCF85063 RTC forced to 24H mode");
}

esp_err_t bsp_rtc_get_datetime(uint8_t *hour, uint8_t *minute, uint8_t *second,
                               uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_addr = 0x04; /* Seconds register address */
    uint8_t data[7] = {0};

    /* Read 7 bytes starting from 0x04: Seconds, Minutes, Hours, Days, Weekdays, Months, Years */
    esp_err_t ret = i2c_master_transmit_receive(pcf85063_dev, &reg_addr, 1, data, 7, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        return ret;
    }

    *second  = bcd2dec(data[0] & 0x7F);
    *minute  = bcd2dec(data[1] & 0x7F);
    *hour    = bcd2dec(data[2] & 0x3F);
    *day     = bcd2dec(data[3] & 0x3F);
    *weekday = bcd2dec(data[4] & 0x07);
    *month   = bcd2dec(data[5] & 0x1F);
    *year    = bcd2dec(data[6]);

    return ESP_OK;
}

/* Sakamoto 算法：由日期计算星期，0=周日（与 PCF85063 weekday 编码一致） */
int bsp_rtc_day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

esp_err_t bsp_rtc_set_datetime(uint8_t year, uint8_t month, uint8_t day, uint8_t weekday,
                               uint8_t hour, uint8_t minute, uint8_t second)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 置 Control_1 STOP 位暂停计数，保证各寄存器原子更新 */
    esp_err_t ret = rtc_pcf85063_write_reg(0x00, 0x20);
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t buf[8] = {
        0x04, /* 起始寄存器地址：Seconds */
        dec2bcd(second), dec2bcd(minute), dec2bcd(hour),
        dec2bcd(day), dec2bcd(weekday), dec2bcd(month), dec2bcd(year),
    };
    ret = i2c_master_transmit(pcf85063_dev, buf, 8, pdMS_TO_TICKS(1000));
    /* 无论写入结果如何都恢复运行（同时回到 24H 模式） */
    esp_err_t ret2 = rtc_pcf85063_write_reg(0x00, 0x00);
    return (ret != ESP_OK) ? ret : ret2;
}
