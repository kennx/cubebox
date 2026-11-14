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

    /* 静态内容一次成图：A-Z 铺满屏（字体测试卡）。
     * 3×5 字形 + 1 间隔 → 每行 6 字符（列 0,4,…,20，恰占满 23 列），
     * 每行 5 点 + 1 空行 → 4 行（行 0,6,12,18，恰占满 23 行）= 24 格；
     * 字母 26 个放不下 → 按约定只写前 24 个（A-X），多余格留空 */
    dotmatrix_grid_t grid;
    dotmatrix_clear(&grid);
    for (int i = 0; i < 24; i++) {
        dotmatrix_draw_char(&grid, (i / 6) * 6, (i % 6) * 4, (char)('A' + i));
    }
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
