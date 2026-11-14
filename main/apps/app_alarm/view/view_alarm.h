#pragma once

#include "lvgl.h"

/* app_alarm 的界面状态与构建/销毁接口（仅 gui_task 上下文调用）。
 * 响铃屏：点阵三行——当前时间（冒号呼吸）、闪烁 "ALARM"、底部 "KEY" 提示
 * （按 KEY 停止响铃）。不挂 swipe_nav：响铃屏不允许滑走，KEY/超时是唯二出口。 */
typedef struct {
    lv_obj_t *screen;      /* 本应用专属屏幕（bg #050505） */
    lv_obj_t *dots;        /* 点阵 widget */
    lv_obj_t *prev_screen; /* 打开前的活动屏幕，销毁时切回 */
    char last_text[8];     /* 上次时间文本，创建时置 "" */
    bool last_blink;       /* 上次 ALARM 行亮灭相位 */
} view_alarm_t;

/* 构建界面并加载自己的屏幕；失败返回 NULL */
view_alarm_t *view_alarm_create(void);

/* 切回 prev_screen 后销毁本应用屏幕与状态；view 可为 NULL */
void view_alarm_destroy(view_alarm_t *view);

/* 重绘：time_text = "HH:MM"/"HH MM"（呼吸冒号）；blink = ALARM 行亮灭相位 */
void view_alarm_render(view_alarm_t *view, const char *time_text, bool blink);
