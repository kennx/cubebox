#pragma once

#include "lvgl.h"

/* app_clock 的界面状态与构建/销毁接口（仅 gui_task 上下文调用） */
typedef struct {
    lv_obj_t *screen;        /* 本应用专属屏幕（bg #050505） */
    lv_obj_t *dots;          /* 点阵 widget */
    lv_obj_t *prev_screen;   /* 打开前的活动屏幕，销毁时切回 */
    char last_text[8];       /* 上次渲染文本（"HH:MM"/"HH MM"），创建时置 "" */
} view_clock_t;

/* 构建界面（点阵屏 + 滑动导航）并加载自己的屏幕；失败返回 NULL */
view_clock_t *view_clock_create(void);

/* 切回 prev_screen 后销毁本应用屏幕与状态；view 可为 NULL */
void view_clock_destroy(view_clock_t *view);

/* 清网格 → 居中画 text → 差量 set_grid → 记录 last_text */
void view_clock_render(view_clock_t *view, const char *text);
