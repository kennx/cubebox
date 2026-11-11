#pragma once

#include "apps/apps.h"

/*
 * 圆角测量研究应用（临时工具，测量完成后可整体删除）：
 *   页 0 全白屏   — 配合正对拍照，测屏幕物理圆角遮罩
 *   页 1 弧线刻度 — 四角同心弧族（r=20..92 步进 4，主刻度 24/48/72 加粗），肉眼直读
 *   页 2 可调单弧 — console `RAD <r>` 远程调半径，弧线贴遮罩边缘即读数
 * 触摸点按循环翻页；左右滑动仍走 swipe_nav 切换应用。
 */
extern const app_lifecycle_t app_radius;

/*
 * 请求设置可调单弧半径并切到页 2（任意任务可调用，console RAD 命令入口）。
 * 经原子 int 交接，gui_task 的 on_running 消费；范围外值被忽略。
 */
void app_radius_request_radius(int radius_px);

/* 请求直接跳页 0-2（console PAGE 命令入口，交接方式同上） */
void app_radius_request_page(int page);
