#pragma once

#include "lvgl.h"

/*
 * 左右滑动应用导航（幻灯片式）：给 app 的 screen 挂 LVGL 内建手势识别，
 * 左滑 → 下一个应用、右滑 → 上一个应用，顺序 = apps 注册表安装顺序，循环。
 * 组件依赖 apps 框架（common 约定：不依赖任何具体 app，可用 apps 框架）。
 */

/* 给 screen 挂手势导航回调（view 创建 screen 后调用；仅 gui_task 上下文） */
void swipe_nav_attach(lv_obj_t *screen);

/*
 * 按安装顺序循环步进：delta=+1 下一个、-1 上一个；无当前应用时打开第 0 个。
 * 内部经 apps_request_open 交接（下一轮 apps_update 生效），
 * 任意任务可安全调用（gui_task 手势回调 / console 任务 NEXT 命令共用）。
 */
void swipe_nav_step(int delta);
