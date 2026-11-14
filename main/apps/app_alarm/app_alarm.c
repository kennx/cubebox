#include <esp_log.h>
#include <stdio.h>
#include <time.h>

#include "lvgl.h"

#include "apps/app_alarm/app_alarm.h"
#include "apps/app_alarm/view/view_alarm.h"
#include "apps/app_clock/app_clock.h"

#define TAG "app_alarm"

static bool s_ringing = false;

void app_alarm_set_ringing(bool ringing)
{
    s_ringing = ringing;
}

static void app_alarm_on_open(app_t *app)
{
    /* 非响铃被滑动轮换/NEXT 误入：不建 UI，本轮请求下一轮退回时钟 */
    if (!s_ringing) {
        apps_request_open(app_clock.name);
        return;
    }
    /* gui_task 上下文：构建并加载响铃屏，句柄存进 user_data */
    app->user_data = view_alarm_create();
    if (app->user_data == NULL) {
        ESP_LOGE(TAG, "view create failed");
    }
}

static void app_alarm_on_running(app_t *app)
{
    view_alarm_t *view = app->user_data;
    if (view == NULL) {
        return;
    }
    /* 与 app_clock 同款呼吸冒号（1 秒亮 1 秒灭）；ALARM 行 500ms 闪烁吸引注意
     * （lv_tick_get 毫秒节拍，on_running 约 10ms 一轮精度足够）。
     * 内容未变零刷新（dotmatrix 差量更新只 invalidate 变化点区域）。 */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char text[6];
    snprintf(text, sizeof(text), "%02d%c%02d", tm.tm_hour,
             (tm.tm_sec % 2 == 0) ? ':' : ' ', tm.tm_min);
    bool blink = (lv_tick_get() / 500) % 2 == 0;
    if (strcmp(text, view->last_text) == 0 && blink == view->last_blink) {
        return;
    }
    view_alarm_render(view, text, blink);
}

static void app_alarm_on_close(app_t *app)
{
    if (app->user_data != NULL) {
        view_alarm_destroy(app->user_data);
        app->user_data = NULL;
    }
}

const app_lifecycle_t app_alarm = {
    .name = "alarm",
    .on_create = NULL,
    .on_open = app_alarm_on_open,
    .on_running = app_alarm_on_running,
    .on_close = app_alarm_on_close,
};
