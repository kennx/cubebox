#include <string.h>

#include "apps/app_clock/view/view_clock.h"
#include "apps/common/dotmatrix/dotmatrix.h"
#include "apps/common/dotmatrix/view_dotmatrix.h"
#include "apps/common/swipe_nav/swipe_nav.h"

view_clock_t *view_clock_create(void)
{
    view_clock_t *view = lv_mem_alloc(sizeof(view_clock_t));
    if (view == NULL) {
        return NULL;
    }
    memset(view, 0, sizeof(*view));
    view->last_text[0] = '\0';

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
    swipe_nav_attach(view->screen);

    lv_scr_load(view->screen);
    return view;
}

void view_clock_render(view_clock_t *view, const char *text)
{
    if (view == NULL || text == NULL) {
        return;
    }
    dotmatrix_grid_t grid;
    dotmatrix_clear(&grid);
    dotmatrix_draw_text_centered(&grid, text);
    dotmatrix_widget_set_grid(view->dots, &grid);
    strlcpy(view->last_text, text, sizeof(view->last_text));
}

void view_clock_destroy(view_clock_t *view)
{
    if (view == NULL) {
        return;
    }
    /* 先切回前一屏再删自己的屏幕，避免删除当前活动屏幕 */
    lv_scr_load(view->prev_screen);
    lv_obj_del(view->screen);
    lv_mem_free(view);
}
