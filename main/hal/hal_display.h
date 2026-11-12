#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/hal_orientation.h"

/**
 * @brief 初始化 LCD（QSPI 总线 + SH8601 面板 + LVGL 显示驱动 + 2ms tick 定时器）
 * @note 内部调用 hal_power_lcd_reset() 完成屏电源复位，需先 hal_power_init()
 */
void hal_display_init(void);

/**
 * @brief 设置背光亮度（发送 SH8601 命令 0x51，无独立背光 GPIO）
 * @param brightness 0-100，线性映射到寄存器 0-255
 */
void hal_display_set_backlight(uint8_t brightness);

/* gui_task 上下文调用；LVGL 软件旋转同时变换显示和触摸坐标。 */
void hal_display_set_orientation(hal_orientation_t orientation);

/**
 * @brief 请求一次屏幕截图（任意任务可调用；经 USB-Serial/JTAG 流式输出）
 * @note gui_task 轮询 hal_display_snapshot_begin_if_requested() 触发全屏 invalidate，
 *       随后 flush_cb 按 "#SNAP x1 y1 x2 y2"+RGB565 hex 条带流出，"#SNAP END" 结束
 */
void hal_display_snapshot_arm(void);

/**
 * @brief gui_task 每循环调用；true 表示本次需要 lv_obj_invalidate(lv_scr_act())
 */
bool hal_display_snapshot_begin_if_requested(void);
