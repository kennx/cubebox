#pragma once

#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief 页面导航请求（任意任务可置位，gui_task 轮询消费——控制台/后续 WiFi 任务用）
 */
typedef enum {
    UI_NAV_NONE = 0,
    UI_NAV_SETTINGS,
    UI_NAV_WIFI,
    UI_NAV_CLOCK,
} ui_nav_req_t;

void ui_nav_request(ui_nav_req_t r);
ui_nav_req_t ui_nav_poll_request(void);

/**
 * @brief 创建常驻设置页 screen（WiFi 行 / 亮度滑条 / 电量行），gui_task 首帧前调用
 */
void ui_settings_create(void);

/**
 * @brief 滑入设置页（250ms 动画）并切换 flush 为纯透传
 */
void ui_settings_show(void);

/**
 * @brief 刷新设置页电量显示（读 AXP2101 并更新标签/电池图形）
 */
void ui_settings_refresh_battery(void);
