#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief 初始化 AXP2101 电源管理（在已创建的 i2c 总线上挂设备并配置输出）
 * @param bus i2c 主总线句柄（由 main.c 创建）
 */
void bsp_power_init(i2c_master_bus_handle_t bus);

/**
 * @brief 获取当前电池电量百分比
 * @param[out] pct 电池电量输出指针 (0-100)，读取失败时回退 99
 * @return esp_err_t
 */
esp_err_t bsp_power_get_battery_pct(uint8_t *pct);

/**
 * @brief LCD 电源复位：ALDO3 高-低-高时序（屏复位由 AXP2101 控制，无独立 RST GPIO）
 */
void bsp_power_lcd_reset(void);
