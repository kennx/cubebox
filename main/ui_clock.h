#pragma once

#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief 创建时钟页 LVGL 对象（状态栏区域），在 gui_task 首帧前调用
 * @param scr 当前活动 screen（lv_scr_act()）
 */
void ui_clock_init(lv_obj_t *scr);

/**
 * @brief 每秒走时刷新：读 RTC 更新日期/时间字符串、读电量、全屏 invalidate
 * @note 由 gui_task 每 1000ms 调用；全屏 invalidate 是像素拦截架构的已知代价（见 AGENTS.md）
 */
void ui_clock_tick_refresh(void);

/**
 * @brief flush_cb 像素叠绘：在 dst（swap_buf）上按 area 叠绘状态栏/日期/时间点阵
 * @param area 本次 flush 区域
 * @param src LVGL 原始像素
 * @param dst 送显目标缓冲（区域外像素原样透传）
 */
void ui_clock_overlay_apply(const lv_area_t *area, const lv_color_t *src, lv_color_t *dst);

/* 状态栏无线状态（Task 4 将随状态栏整体移除） */
void statusbar_set_wifi_state(bool connected);
void statusbar_set_ble_state(bool active);
