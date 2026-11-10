#pragma once

#include "lvgl.h"

#include "dotmatrix.h"

/*
 * 点阵 LVGL 自定义 widget：单个对象绘制 23×23 圆点阵，按裁剪区域差量画圆。
 * 颜色经 style 参数注入（配置点）；背景透明，屏底色由所在 screen 样式负责。
 */
typedef struct {
    lv_color_t dot_on;   /* 亮点色 */
    lv_color_t dot_off;  /* 灭点色 */
} dotmatrix_style_t;

/* 默认配色（spec：亮=白、灭=#151515、屏底=#050505） */
#define DOTMATRIX_COLOR_ON  lv_color_white()
#define DOTMATRIX_COLOR_OFF lv_color_make(0x15, 0x15, 0x15)
#define DOTMATRIX_BG_COLOR  lv_color_make(0x05, 0x05, 0x05)

/*
 * 创建全屏（480×480）点阵 widget；style == NULL 用默认配色；初始全灭。
 * 已置 CLICKABLE|GESTURE_BUBBLE：手势事件冒泡给 screen（配合 swipe_nav_attach）。
 */
lv_obj_t *dotmatrix_widget_create(lv_obj_t *parent, const dotmatrix_style_t *style);

/*
 * 差量更新：与内部网格逐位比较，仅 invalidate 变化点外接方域后整体拷贝。
 * 仅 gui_task 上下文调用。
 */
void dotmatrix_widget_set_grid(lv_obj_t *widget, const dotmatrix_grid_t *grid);
