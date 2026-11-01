#pragma once

#include "lvgl.h"

/**
 * @brief 每秒走时刷新：读 RTC 更新日期/时间字符串并全屏 invalidate
 * @note 由 gui_task 每 1000ms 调用；全屏 invalidate 是像素拦截架构的已知代价（见 AGENTS.md）
 */
void ui_clock_tick_refresh(void);

/**
 * @brief flush_cb 像素叠绘：在 dst（swap_buf）上按 area 叠绘日期/时间点阵
 * @param area 本次 flush 区域
 * @param src LVGL 原始像素
 * @param dst 送显目标缓冲（区域外像素原样透传）
 */
void ui_clock_overlay_apply(const lv_area_t *area, const lv_color_t *src, lv_color_t *dst);
