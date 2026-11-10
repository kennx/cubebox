#pragma once

#include "driver/i2c_master.h"

/**
 * @brief 初始化 AXP2101 电源管理（在已创建的 i2c 总线上挂设备并配置输出）
 * @param bus i2c 主总线句柄（由 main.c 创建）
 */
void hal_power_init(i2c_master_bus_handle_t bus);

/**
 * @brief LCD 电源复位：ALDO3 高-低-高时序（屏复位由 AXP2101 控制，无独立 RST GPIO）
 */
void hal_power_lcd_reset(void);
