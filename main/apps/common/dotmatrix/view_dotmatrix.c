/*
 * 点阵 LVGL v8 自定义 widget：继承 lv_obj_class，DRAW_MAIN 中遍历 529 点、
 * 只画与裁剪区相交的圆（lv_draw_rect radius）。几何对照 example.svg：
 * 边距 4px、半径 8px、圆心 c(i) = 12 + (i*456 + 11)/22（与 SVG 常量差 < 1px/点）。
 */
#include "apps/common/dotmatrix/view_dotmatrix.h"

/* 几何常量（px） */
#define DOTMATRIX_DOT_R     8     /* 圆半径 */
#define DOTMATRIX_ORIGIN    12    /* 首圆心坐标（= 边距 4 + 半径 8） */
#define DOTMATRIX_PITCH_NUM 456   /* 480 − 2×12 */
#define DOTMATRIX_PITCH_DEN 22    /* 23 点 − 1 */

typedef struct {
    lv_obj_t obj;             /* 必须首成员 */
    dotmatrix_grid_t grid;    /* 当前显示内容 */
    dotmatrix_style_t style;
} dotmatrix_widget_t;

extern const lv_obj_class_t dotmatrix_widget_class;

/* 圆心按屏幕绝对坐标计算（LVGL invalidate/draw 均为绝对坐标语义）——
 * 前提：widget 全屏铺在 (0,0)。若日后复用于非原点容器，需改为叠加
 * widget 自身边偏移。 */
static lv_coord_t dot_center(int i)
{
    return (lv_coord_t)(DOTMATRIX_ORIGIN +
                        (i * DOTMATRIX_PITCH_NUM + DOTMATRIX_PITCH_DEN / 2) / DOTMATRIX_PITCH_DEN);
}

static void dotmatrix_widget_constructor_cb(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    LV_UNUSED(class_p);
    dotmatrix_widget_t *w = (dotmatrix_widget_t *)obj;
    dotmatrix_clear(&w->grid);
    w->style = (dotmatrix_style_t){ DOTMATRIX_COLOR_ON, DOTMATRIX_COLOR_OFF };
    /* 承接按压 + 手势冒泡到 screen（swipe_nav 挂 screen 收 LV_EVENT_GESTURE） */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void dotmatrix_widget_draw(lv_obj_t *obj, lv_event_t *e)
{
    dotmatrix_widget_t *w = (dotmatrix_widget_t *)obj;
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    const lv_area_t *clip = draw_ctx->clip_area;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = DOTMATRIX_DOT_R;

    for (int row = 0; row < DOTMATRIX_ROWS; row++) {
        for (int col = 0; col < DOTMATRIX_COLS; col++) {
            lv_coord_t cx = dot_center(col);
            lv_coord_t cy = dot_center(row);
            lv_area_t a = { cx - DOTMATRIX_DOT_R, cy - DOTMATRIX_DOT_R,
                            cx + DOTMATRIX_DOT_R, cy + DOTMATRIX_DOT_R };
            if (a.x2 < clip->x1 || a.x1 > clip->x2 || a.y2 < clip->y1 || a.y1 > clip->y2) {
                continue; /* 与裁剪区不相交，本轮不画 */
            }
            dsc.bg_color = dotmatrix_get(&w->grid, row, col) ? w->style.dot_on : w->style.dot_off;
            lv_draw_rect(draw_ctx, &dsc, &a);
        }
    }
}

static void dotmatrix_widget_event_cb(const lv_obj_class_t *class_p, lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
        dotmatrix_widget_draw(lv_event_get_target(e), e);
        return;
    }
    lv_obj_event_base(class_p, e); /* 其余事件交回基类 */
}

const lv_obj_class_t dotmatrix_widget_class = {
    .constructor_cb = dotmatrix_widget_constructor_cb,
    .event_cb = dotmatrix_widget_event_cb,
    .width_def = 480,
    .height_def = 480,
    .instance_size = sizeof(dotmatrix_widget_t),
    .base_class = &lv_obj_class,
};

lv_obj_t *dotmatrix_widget_create(lv_obj_t *parent, const dotmatrix_style_t *style)
{
    lv_obj_t *obj = lv_obj_class_create_obj(&dotmatrix_widget_class, parent);
    if (obj == NULL) {
        return NULL; /* OOM：必须在 init_obj 解引用之前返回 */
    }
    lv_obj_class_init_obj(obj);
    if (style != NULL) {
        ((dotmatrix_widget_t *)obj)->style = *style;
    }
    return obj;
}

void dotmatrix_widget_set_grid(lv_obj_t *obj, const dotmatrix_grid_t *grid)
{
    if (obj == NULL || grid == NULL) {
        return;
    }
    dotmatrix_widget_t *w = (dotmatrix_widget_t *)obj;
    for (int row = 0; row < DOTMATRIX_ROWS; row++) {
        for (int col = 0; col < DOTMATRIX_COLS; col++) {
            if (dotmatrix_get(&w->grid, row, col) != dotmatrix_get(grid, row, col)) {
                lv_coord_t cx = dot_center(col);
                lv_coord_t cy = dot_center(row);
                lv_area_t a = { cx - DOTMATRIX_DOT_R - 1, cy - DOTMATRIX_DOT_R - 1,
                                cx + DOTMATRIX_DOT_R + 1, cy + DOTMATRIX_DOT_R + 1 };
                lv_obj_invalidate_area(obj, &a); /* 只刷新变化点（半径+1 覆盖抗锯齿边） */
            }
        }
    }
    w->grid = *grid;
}
