#pragma once

#include <stdbool.h>

#include "apps/apps.h"

/* 闹钟响铃屏：当前时间（冒号呼吸）+ 闪烁 ALARM + KEY 停止提示。
 * 由 main.c 的 alarm_service 在触发时 apps_request_open 切入；
 * KEY 按下或 3 分钟超时后切回时钟。声音起停在 gui_task 胶水层（main.c），
 * 本应用只负责显示。说明文档见 docs/apps/app_alarm.md。 */
extern const app_lifecycle_t app_alarm;

/**
 * @brief 告知本应用当前是否处于响铃态（gui_task 在 apps_request_open 前调用）
 * @note 响铃态才构建响铃 UI；非响铃被滑动轮换/NEXT 误入时 on_open 立即退回时钟
 *       （请求标志模式，对照 app_radius_request_radius 先例）。
 */
void app_alarm_set_ringing(bool ringing);
