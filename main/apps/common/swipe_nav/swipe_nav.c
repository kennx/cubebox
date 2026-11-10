#include <string.h>

#include "lvgl.h"

#include "apps/apps.h"
#include "apps/common/swipe_nav/swipe_nav.h"

static void swipe_nav_gesture_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        swipe_nav_step(1);  /* 左滑：滑向下一个应用 */
    } else if (dir == LV_DIR_RIGHT) {
        swipe_nav_step(-1); /* 右滑：回到上一个应用 */
    }
    lv_event_stop_bubbling(e); /* 手势已消费，不再向外冒泡 */
}

void swipe_nav_attach(lv_obj_t *screen)
{
    if (screen == NULL) {
        return;
    }
    lv_obj_add_event_cb(screen, swipe_nav_gesture_cb, LV_EVENT_GESTURE, NULL);
}

void swipe_nav_step(int delta)
{
    size_t n = apps_count();
    if (n == 0) {
        return;
    }
    int cur = 0;
    const char *name = apps_current_name();
    if (name != NULL) {
        for (size_t i = 0; i < n; i++) {
            if (strcmp(apps_name_at(i), name) == 0) {
                cur = (int)i;
                break;
            }
        }
    }
    int target = ((cur + delta) % (int)n + (int)n) % (int)n; /* 循环步进 */
    apps_request_open(apps_name_at((size_t)target));
}
