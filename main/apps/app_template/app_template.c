/*
 * App 生命周期模板。本应用不会被安装，仅作为新应用的复制起点
 * （对照 StackChan firmware/main/apps/app_template/app_template.cpp）。
 */
#include <esp_log.h>

#include "apps/apps.h"
#include "apps/app_template/app_template.h"

#define TAG "app_template"

/* 安装时调用一次（apps_install 触发）。可能在任意任务上下文，不得触碰 LVGL */
static void app_template_on_create(app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "on create");
}

/* 打开时调用：构建 UI、初始化操作。仅 gui_task 上下文（LVGL 可用） */
static void app_template_on_open(app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "on open");
}

/* 运行期间被 gui_task 主循环反复调用。仅 gui_task 上下文，勿做耗时操作 */
static void app_template_on_running(app_t *app)
{
    (void)app;
}

/* 关闭/被切换时调用：销毁 UI、释放资源。仅 gui_task 上下文 */
static void app_template_on_close(app_t *app)
{
    (void)app;
    ESP_LOGI(TAG, "on close");
}

const app_lifecycle_t app_template = {
    .name = "template",
    .on_create = app_template_on_create,
    .on_open = app_template_on_open,
    .on_running = app_template_on_running,
    .on_close = app_template_on_close,
};
