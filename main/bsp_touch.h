#pragma once

#include <stdint.h>
#include "lvgl.h"
#include "driver/i2c_master.h"

/**
 * @brief 消费一次待处理的滑动事件（gui_task 每循环调用，返回 0=无）
 * @note 检测在 indev read 回调内完成，但动作必须在 LVGL 输入处理之外执行——
 *       在 read_cb 里直接切屏会卡死 LVGL（实测冻结），故走"检测置位/主循环消费"模式
 */
lv_dir_t bsp_touch_consume_swipe(void);

/**
 * @brief 初始化 CST9220 触摸（cst9217 驱动组件）并注册 LVGL pointer indev
 * @param bus i2c 主总线句柄（TP 地址 0x5A，RST=GPIO11，INT=GPIO5）
 * @note 需在 bsp_lcd_init()（LVGL 已初始化）之后调用
 */
void bsp_touch_init(i2c_master_bus_handle_t bus);

/**
 * @brief 最近一次触摸活动的 tick 毫秒数（低功耗判定用）
 */
uint32_t bsp_touch_last_activity_ms(void);

/**
 * @brief 置位：下一次 press 只唤醒屏幕、不作为点击上报给 LVGL（防低功耗唤醒误触）
 */
void bsp_touch_set_swallow_once(void);

/**
 * @brief 调试：直接轮询触摸芯片 seconds 秒并统计（有点/无 ACK/I2C 错误），期间触摸屏幕
 */
void bsp_touch_debug_poll(int seconds);
