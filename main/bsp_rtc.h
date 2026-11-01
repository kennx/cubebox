#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief 初始化 PCF85063 RTC（挂到已创建的 i2c 总线并强制 24H 模式）
 * @param bus i2c 主总线句柄（由 main.c 创建）
 */
void bsp_rtc_init(i2c_master_bus_handle_t bus);

/**
 * @brief 读取 RTC 日期时间
 * @note year 为 2000 系年后两位（0-99），weekday 0=周日（芯片编码）
 */
esp_err_t bsp_rtc_get_datetime(uint8_t *hour, uint8_t *minute, uint8_t *second,
                               uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year);

/**
 * @brief 写入 RTC 日期时间（STOP 位保证原子更新，写完恢复运行）
 */
esp_err_t bsp_rtc_set_datetime(uint8_t year, uint8_t month, uint8_t day, uint8_t weekday,
                               uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief Sakamoto 算法：由日期计算星期，0=周日（与 PCF85063 weekday 编码一致）
 */
int bsp_rtc_day_of_week(int y, int m, int d);

extern const char *const bsp_rtc_weekdays_str[7];
extern const char *const bsp_rtc_months_str[12];
