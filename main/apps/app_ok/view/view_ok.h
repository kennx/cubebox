#pragma once

#include "lvgl.h"

/* app_ok 的界面状态与构建/销毁接口（仅 gui_task 上下文调用） */
typedef struct {
    lv_obj_t *screen;      /* 本应用专属屏幕 */
    lv_obj_t *label;
    lv_obj_t *prev_screen; /* 打开前的活动屏幕，销毁时切回 */
} view_ok_t;

/* 构建界面（黑底白字居中「OK」）并加载自己的屏幕；失败返回 NULL */
view_ok_t *view_ok_create(void);

/* 切回 prev_screen 后销毁本应用屏幕与状态；view 可为 NULL */
void view_ok_destroy(view_ok_t *view);
