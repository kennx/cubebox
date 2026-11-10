#include "apps/app_ok/view/view_ok.h"

LV_FONT_DECLARE(lv_font_montserrat_48);

view_ok_t *view_ok_create(void)
{
    view_ok_t *view = lv_mem_alloc(sizeof(view_ok_t));
    if (view == NULL) {
        return NULL;
    }

    view->prev_screen = lv_scr_act();
    view->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(view->screen, lv_color_black(), 0);

    view->label = lv_label_create(view->screen);
    lv_obj_set_style_text_font(view->label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(view->label, lv_color_white(), 0);
    lv_label_set_text(view->label, "OK");
    lv_obj_center(view->label);

    lv_scr_load(view->screen);
    return view;
}

void view_ok_destroy(view_ok_t *view)
{
    if (view == NULL) {
        return;
    }
    /* 先切回前一屏再删自己的屏幕，避免删除当前活动屏幕 */
    lv_scr_load(view->prev_screen);
    lv_obj_del(view->screen);
    lv_mem_free(view);
}
