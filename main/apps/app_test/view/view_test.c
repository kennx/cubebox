#include <string.h>

#include "apps/app_test/view/view_test.h"
#include "apps/common/dotmatrix/dotmatrix.h"
#include "apps/common/dotmatrix/view_dotmatrix.h"
#include "apps/common/swipe_nav/swipe_nav.h"

view_test_t *view_test_create(void)
{
    view_test_t *view = lv_mem_alloc(sizeof(view_test_t));
    if (view == NULL) {
        return NULL;
    }
    memset(view, 0, sizeof(*view));

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

    /* 静态内容一次成图：居中点阵「OK」 */
    dotmatrix_grid_t grid;
    dotmatrix_clear(&grid);
    dotmatrix_draw_text_centered(&grid, "OK");
    dotmatrix_widget_set_grid(view->dots, &grid);

    swipe_nav_attach(view->screen);
    lv_scr_load(view->screen);
    return view;
}

void view_test_destroy(view_test_t *view)
{
    if (view == NULL) {
        return;
    }
    /* 先切回前一屏再删自己的屏幕，避免删除当前活动屏幕 */
    lv_scr_load(view->prev_screen);
    lv_obj_del(view->screen);
    lv_mem_free(view);
}
