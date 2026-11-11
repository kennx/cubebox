/*
 * 圆角测量 UI：三页屏幕 + 自绘弧线 widget。
 *
 * 自绘 widget 对照 dotmatrix 的 class 继承模式：DRAW_MAIN 中以 lv_draw_line
 * 逐段画弧（3°/段），无持久点表、无额外缓冲；静态图案只在 invalidate 后重画。
 * 弧线几何：圆角遮罩的圆心在角点向内 (r,r) 处，四角各画朝向该角的 90° 弧。
 */
#include <string.h>

#include "lvgl.h"
#include "apps/app_radius/view/view_radius.h"
#include "apps/common/swipe_nav/swipe_nav.h"

#define RADIUS_SCREEN_W 480
#define RADIUS_SCREEN_H 480
#define ARC_STEP_DEG   3   /* 每段 3°（lv_trigo_sin 角度单位为 1°） */

/* 刻度族：r = 20..92 步进 4；主刻度（%24==0，即 24/48/72）3px，其余 1px */
#define SCALE_R_MIN   20
#define SCALE_R_MAX   92
#define SCALE_R_STEP  4
#define SCALE_W_MAJOR 3
#define SCALE_W_MINOR 1

typedef struct {
    lv_obj_t obj;
    uint8_t mode;   /* 0=刻度族 1=可调单弧 */
    int16_t radius; /* mode 1 的半径 */
} radius_widget_t;

extern const lv_obj_class_t radius_widget_class;

static lv_point_t arc_point(int32_t cx, int32_t cy, int32_t r, int16_t angle_deg)
{
    lv_point_t p;
    p.x = (lv_coord_t)(cx + (int32_t)r * lv_trigo_sin((int16_t)(angle_deg + 90)) / 32767);
    p.y = (lv_coord_t)(cy + (int32_t)r * lv_trigo_sin(angle_deg) / 32767);
    return p;
}

/* 画一条 90° 弧（朝向某个角），角度基 base_deg 起 90° */
static void draw_arc(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc,
                     int32_t cx, int32_t cy, int32_t r, int16_t base_deg)
{
    const lv_area_t *clip = ctx->clip_area;
    for (int16_t a = base_deg; a < base_deg + 90; a += ARC_STEP_DEG) {
        int16_t a2 = (int16_t)(a + ARC_STEP_DEG);
        if (a2 > base_deg + 90) {
            a2 = (int16_t)(base_deg + 90);
        }
        lv_point_t p1 = arc_point(cx, cy, r, a);
        lv_point_t p2 = arc_point(cx, cy, r, a2);
        /* 粗判：段包围盒与裁剪区不相交则跳过（含线宽余量） */
        if (LV_MAX(p1.x, p2.x) + dsc->width < clip->x1 || LV_MIN(p1.x, p2.x) - dsc->width > clip->x2 ||
            LV_MAX(p1.y, p2.y) + dsc->width < clip->y1 || LV_MIN(p1.y, p2.y) - dsc->width > clip->y2) {
            continue;
        }
        lv_draw_line(ctx, dsc, &p1, &p2);
    }
}

/* 四角各画一条半径 r 的弧：左上 180°起、右上 270°、右下 0°、左下 90° */
static void draw_corner_arcs(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc, int32_t r)
{
    draw_arc(ctx, dsc, r, r, r, 180);
    draw_arc(ctx, dsc, RADIUS_SCREEN_W - r, r, r, 270);
    draw_arc(ctx, dsc, RADIUS_SCREEN_W - r, RADIUS_SCREEN_H - r, r, 0);
    draw_arc(ctx, dsc, r, RADIUS_SCREEN_H - r, r, 90);
}

static void radius_widget_draw(lv_obj_t *obj, lv_event_t *e)
{
    radius_widget_t *w = (radius_widget_t *)obj;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;

    if (w->mode == 0) {
        /* 刻度族：细线为主刻度铺路，肉眼看哪圈弧与遮罩边缘重合 */
        for (int r = SCALE_R_MIN; r <= SCALE_R_MAX; r += SCALE_R_STEP) {
            dsc.width = (r % 24 == 0) ? SCALE_W_MAJOR : SCALE_W_MINOR;
            draw_corner_arcs(ctx, &dsc, r);
        }
    } else {
        dsc.width = 2;
        draw_corner_arcs(ctx, &dsc, w->radius);
    }
}

static void radius_widget_event_cb(const lv_obj_class_t *class_p, lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
        radius_widget_draw(lv_event_get_target(e), e);
        return;
    }
    lv_obj_event_base(class_p, e);
}

const lv_obj_class_t radius_widget_class = {
    .constructor_cb = NULL,
    .event_cb = radius_widget_event_cb,
    .width_def = RADIUS_SCREEN_W,
    .height_def = RADIUS_SCREEN_H,
    .instance_size = sizeof(radius_widget_t),
    .base_class = &lv_obj_class,
};

static lv_obj_t *radius_widget_create(lv_obj_t *parent, uint8_t mode)
{
    lv_obj_t *obj = lv_obj_class_create_obj(&radius_widget_class, parent);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_class_init_obj(obj);
    ((radius_widget_t *)obj)->mode = mode;
    ((radius_widget_t *)obj)->radius = 40;
    /* widget 自身不响应输入，点击穿透到 screen（翻页） */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

struct view_radius {
    lv_obj_t *prev_screen;
    lv_obj_t *screens[3]; /* 0 白屏 1 刻度 2 可调弧 */
    int page;
};

static void screen_click_cb(lv_event_t *e)
{
    view_radius_t *view = lv_event_get_user_data(e);
    if (view != NULL) {
        view_radius_next_page(view);
    }
}

/* 黑底测量页：挂手势导航 + 点按翻页 + 弧线 widget */
static lv_obj_t *measure_screen_create(view_radius_t *view, uint8_t mode)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    if (scr == NULL) {
        return NULL;
    }
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_click_cb, LV_EVENT_CLICKED, view);
    swipe_nav_attach(scr);

    if (radius_widget_create(scr, mode) == NULL) {
        lv_obj_del(scr);
        return NULL;
    }
    return scr;
}

view_radius_t *view_radius_create(void)
{
    view_radius_t *view = lv_mem_alloc(sizeof(view_radius_t));
    if (view == NULL) {
        return NULL;
    }
    memset(view, 0, sizeof(*view));
    view->page = 0;

    /* 页 0：全白屏（拍照分析用，AMOLED 全白峰值电流高，测量完即切页） */
    view->prev_screen = lv_scr_act();
    view->screens[0] = lv_obj_create(NULL);
    if (view->screens[0] == NULL) {
        lv_mem_free(view);
        return NULL;
    }
    lv_obj_set_style_bg_color(view->screens[0], lv_color_white(), 0);
    lv_obj_add_flag(view->screens[0], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(view->screens[0], screen_click_cb, LV_EVENT_CLICKED, view);
    swipe_nav_attach(view->screens[0]);

    /* 页 1/2：黑底弧线 */
    view->screens[1] = measure_screen_create(view, 0);
    view->screens[2] = measure_screen_create(view, 1);
    if (view->screens[1] == NULL || view->screens[2] == NULL) {
        view_radius_destroy(view);
        return NULL;
    }

    lv_scr_load(view->screens[0]);
    return view;
}

void view_radius_destroy(view_radius_t *view)
{
    if (view == NULL) {
        return;
    }
    lv_scr_load(view->prev_screen);
    for (int i = 0; i < 3; i++) {
        if (view->screens[i] != NULL) {
            lv_obj_del(view->screens[i]);
        }
    }
    lv_mem_free(view);
}

void view_radius_next_page(view_radius_t *view)
{
    if (view == NULL) {
        return;
    }
    view->page = (view->page + 1) % 3;
    lv_scr_load(view->screens[view->page]);
}

void view_radius_set_page(view_radius_t *view, int page)
{
    if (view == NULL || page < 0 || page > 2) {
        return;
    }
    view->page = page;
    lv_scr_load(view->screens[page]);
}

void view_radius_set_radius(view_radius_t *view, int radius_px)
{
    if (view == NULL || radius_px < 1 || radius_px > 240) {
        return;
    }
    /* widget 首成员是 obj；找到页 2 的 widget（screen 唯一子对象） */
    lv_obj_t *child = lv_obj_get_child(view->screens[2], 0);
    if (child == NULL) {
        return;
    }
    radius_widget_t *w = (radius_widget_t *)child;
    if (w->radius != radius_px) {
        w->radius = (int16_t)radius_px;
        lv_obj_invalidate(child);
    }
    lv_scr_load(view->screens[2]); /* 跳到可调弧页 */
    view->page = 2;
}
