#include <esp_log.h>
#include <stdio.h>
#include <time.h>

#include "apps/app_clock/app_clock.h"
#include "apps/app_clock/view/view_clock.h"

#define TAG "app_clock"

static void app_clock_on_open(app_t *app)
{
    /* gui_task 上下文：构建并加载自己的屏幕，句柄存进 user_data */
    app->user_data = view_clock_create();
    if (app->user_data == NULL) {
        ESP_LOGE(TAG, "view create failed");
    }
}

static void app_clock_on_running(app_t *app)
{
    view_clock_t *view = app->user_data;
    if (view == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    /* 无状态全量重算显示文本：冒号亮 = 偶数秒（1 秒亮 1 秒灭呼吸）；
     * 灭时用等宽空格替换冒号，布局不变（呼吸只差 2 个点） */
    char text[6];
    snprintf(text, sizeof(text), "%02d%c%02d", tm.tm_hour,
             (tm.tm_sec % 2 == 0) ? ':' : ' ', tm.tm_min);
    if (strcmp(text, view->last_text) == 0) {
        return; /* 内容未变零刷新（on_running 约 10ms 一轮） */
    }
    view_clock_render(view, text);
}

static void app_clock_on_close(app_t *app)
{
    /* gui_task 上下文：切回前一屏并释放本应用资源 */
    if (app->user_data != NULL) {
        view_clock_destroy(app->user_data);
        app->user_data = NULL;
    }
}

const app_lifecycle_t app_clock = {
    .name = "clock",
    .on_create = NULL,
    .on_open = app_clock_on_open,
    .on_running = app_clock_on_running,
    .on_close = app_clock_on_close,
};
