/*
 * 圆角测量应用：生命周期 + console → gui 的原子参数交接。
 * UI 全部在 view/view_radius.c（app 主文件不直接调用 LVGL）。
 */
#include <esp_log.h>

#include "apps/apps.h"
#include "apps/app_radius/app_radius.h"
#include "apps/app_radius/view/view_radius.h"

#define TAG "app_radius"

/* console 任务写、gui_task 读：对齐 int 读写原子，volatile 禁止缓存 */
static volatile int s_req_radius = -1; /* -1 = 无请求；否则为请求的半径 px */
static volatile int s_req_page = -1;   /* -1 = 无请求；否则为请求的页号 0-2 */

void app_radius_request_radius(int radius_px)
{
    if (radius_px >= 1 && radius_px <= 240) {
        s_req_radius = radius_px;
    }
}

void app_radius_request_page(int page)
{
    if (page >= 0 && page <= 2) {
        s_req_page = page;
    }
}

static void app_radius_on_open(app_t *app)
{
    /* gui_task 上下文：构建三页测量 UI */
    app->user_data = view_radius_create();
    if (app->user_data == NULL) {
        ESP_LOGE(TAG, "view create failed");
    }
}

static void app_radius_on_running(app_t *app)
{
    /* gui_task 上下文：消费 console 的半径/翻页请求（有请求才动 UI） */
    if (app->user_data == NULL) {
        return;
    }
    if (s_req_radius >= 0) {
        view_radius_set_radius(app->user_data, s_req_radius);
        s_req_radius = -1;
    }
    if (s_req_page >= 0) {
        view_radius_set_page(app->user_data, s_req_page);
        s_req_page = -1;
    }
}

static void app_radius_on_close(app_t *app)
{
    if (app->user_data != NULL) {
        view_radius_destroy(app->user_data);
        app->user_data = NULL;
    }
}

const app_lifecycle_t app_radius = {
    .name = "radius",
    .on_create = NULL,
    .on_open = app_radius_on_open,
    .on_running = app_radius_on_running,
    .on_close = app_radius_on_close,
};
