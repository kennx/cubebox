#include <esp_log.h>

#include "apps/apps.h"
#include "apps/app_ok/app_ok.h"
#include "apps/app_ok/view/view_ok.h"

#define TAG "app_ok"

static void app_ok_on_open(app_t *app)
{
    /* gui_task 上下文：构建并加载自己的屏幕，句柄存进 user_data */
    app->user_data = view_ok_create();
    if (app->user_data == NULL) {
        ESP_LOGE(TAG, "view create failed");
    }
}

static void app_ok_on_close(app_t *app)
{
    /* gui_task 上下文：切回前一屏并释放本应用资源 */
    if (app->user_data != NULL) {
        view_ok_destroy(app->user_data);
        app->user_data = NULL;
    }
}

/* 静态画面：on_create / on_running 无事可做，置 NULL（对照 app_template） */
const app_lifecycle_t app_ok = {
    .name = "ok",
    .on_create = NULL,
    .on_open = app_ok_on_open,
    .on_running = NULL,
    .on_close = app_ok_on_close,
};
