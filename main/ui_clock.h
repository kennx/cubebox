#pragma once

#include "lvgl.h"

/**
 * @brief 创建时钟页 screen（黑底 + 全屏透明手势对象）并设为活动页
 * @note gui_task 首帧前调用一次；上滑手势打开设置页
 */
void ui_clock_init(void);

/**
 * @brief 滑回时钟页（250ms 动画）并切回像素叠绘模式
 */
void ui_clock_show(void);

/**
 * @brief 每秒走时刷新：读 RTC 更新日期/时间字符串并全屏 invalidate
 * @note 由 gui_task 每 1000ms 调用；非时钟页时跳过（省 I2C 与全屏重绘）。
 *       全屏 invalidate 是像素拦截架构的已知代价（见 AGENTS.md）
 */
void ui_clock_tick_refresh(void);

/**
 * @brief flush_cb 像素叠绘：在 dst（swap_buf）上按 area 叠绘日期/时间点阵
 * @param area 本次 flush 区域
 * @param src LVGL 原始像素
 * @param dst 送显目标缓冲（区域外像素原样透传）
 */
void ui_clock_overlay_apply(const lv_area_t *area, const lv_color_t *src, lv_color_t *dst);
