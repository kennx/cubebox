#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 圆角测量 UI（仅 gui_task 上下文调用）：
 *   create/destroy  — 构建三页屏幕（白屏 / 弧线刻度 / 可调单弧）
 *   next_page       — 触摸点按翻页（0→1→2→0 循环）
 *   set_radius      — 设置可调单弧半径并跳页 2（console RAD 命令消费侧）
 *   set_page        — 直接跳页 0-2（console PAGE 命令消费侧）
 */
typedef struct view_radius view_radius_t;

view_radius_t *view_radius_create(void);
void view_radius_destroy(view_radius_t *view);
void view_radius_next_page(view_radius_t *view);
void view_radius_set_radius(view_radius_t *view, int radius_px);
void view_radius_set_page(view_radius_t *view, int page);
