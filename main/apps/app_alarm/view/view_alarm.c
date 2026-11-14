#include <string.h>

#include "apps/app_alarm/view/view_alarm.h"
#include "apps/common/dotmatrix/dotmatrix.h"
#include "apps/common/dotmatrix/view_dotmatrix.h"

/* 在 row 行水平居中画 text（dotmatrix_draw_text_centered 的行固定 9，多行布局需自算列） */
static void draw_text_at_row(dotmatrix_grid_t *g, const char *text, int row)
{
    int cols = dotmatrix_text_cols(text);
    int col = (DOTMATRIX_COLS - cols) / 2;
    for (const char *p = text; *p != '\0'; p++) {
        dotmatrix_draw_char(g, row, col, *p);
        col += 4; /* 3 列字形 + 1 列间隔 */
    }
}

view_alarm_t *view_alarm_create(void)
{
    view_alarm_t *view = lv_mem_alloc(sizeof(view_alarm_t));
    if (view == NULL) {
        return NULL;
    }
    memset(view, 0, sizeof(*view));
    view->last_text[0] = '\0';
    view->last_blink = true;

    view->prev_screen = lv_scr_act();
    view->screen = lv_obj_create(NULL);
    if (view->screen == NULL) {
        lv_mem_free(view);
        return NULL;
    }
    lv_obj_set_style_bg_color(view->screen, DOTMATRIX_BG_COLOR, 0);

    view->dots = dotmatrix_widget_create(view->screen, NULL);
    if (view->dots == NULL) {
        lv_obj_del(view->screen);
        lv_mem_free(view);
        return NULL;
    }

    lv_scr_load(view->screen);
    return view;
}

void view_alarm_render(view_alarm_t *view, const char *time_text, bool blink)
{
    if (view == NULL || time_text == NULL) {
        return;
    }
    dotmatrix_grid_t grid;
    dotmatrix_clear(&grid);
    draw_text_at_row(&grid, time_text, 2);     /* 时间（行 2-6） */
    if (blink) {
        draw_text_at_row(&grid, "ALARM", 9);   /* 闪烁标识（行 9-13） */
    }
    draw_text_at_row(&grid, "KEY", 16);        /* 停止提示（行 16-20） */
    dotmatrix_widget_set_grid(view->dots, &grid);
    strlcpy(view->last_text, time_text, sizeof(view->last_text));
    view->last_blink = blink;
}

void view_alarm_destroy(view_alarm_t *view)
{
    if (view == NULL) {
        return;
    }
    /* 先切回前一屏再删自己的屏幕，避免删除当前活动屏幕 */
    lv_scr_load(view->prev_screen);
    lv_obj_del(view->screen);
    lv_mem_free(view);
}
