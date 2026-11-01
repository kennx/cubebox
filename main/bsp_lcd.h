#pragma once

#include <stdint.h>

/**
 * @brief 初始化 LCD（SPI/QSPI 总线 + SH8601 面板 + LVGL 显示驱动 + 2ms tick 定时器）
 * @note 内部调用 bsp_power_lcd_reset() 完成屏电源复位，需先 bsp_power_init()
 */
void bsp_lcd_init(void);

/**
 * @brief 设置背光亮度（发送 SH8601 命令 0x51，无独立背光 GPIO）
 * @param brightness 0-100，线性映射到寄存器 0-255
 */
void bsp_lcd_set_backlight(uint8_t brightness);
