/*
 * App 注册表与生命周期状态机。
 * 本文件刻意保持纯 C（不依赖 ESP-IDF/LVGL），供 tests/ 主机端测试直接编译
 * （tests/CMakeLists.txt，对照 StackChan firmware/tests 的做法）。
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "apps/apps.h"

static const app_lifecycle_t *s_apps[APPS_MAX];  /* 已安装应用的描述符 */
static app_t s_instances[APPS_MAX];              /* 各应用实例（api + user_data） */
static int s_current = -1;                       /* 当前打开的应用下标，-1 无 */

/* 跨任务切换请求：生产者（console 等）先写 payload 再置位，gui_task 消费 */
static char s_pending[APP_NAME_MAX];
static atomic_bool s_pending_valid = false;

static int apps_find(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    for (int i = 0; i < APPS_MAX; i++) {
        if (s_apps[i] != NULL && strcmp(s_apps[i]->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool apps_install(const app_lifecycle_t *api)
{
    if (api == NULL || api->name == NULL || api->name[0] == '\0') {
        return false;
    }
    if (apps_find(api->name) >= 0) {
        return false; /* 重名拒绝 */
    }
    for (int i = 0; i < APPS_MAX; i++) {
        if (s_apps[i] == NULL) {
            s_apps[i] = api;
            s_instances[i].api = api;
            s_instances[i].user_data = NULL;
            if (api->on_create != NULL) {
                api->on_create(&s_instances[i]);
            }
            return true;
        }
    }
    return false; /* 表满 */
}

bool apps_open(const char *name)
{
    int idx = apps_find(name);
    if (idx < 0) {
        return false;
    }
    if (s_current == idx) {
        return true; /* 幂等：已在打开 */
    }
    if (s_current >= 0) {
        const app_lifecycle_t *old = s_apps[s_current];
        if (old->on_close != NULL) {
            old->on_close(&s_instances[s_current]);
        }
        s_current = -1;
    }
    s_current = idx;
    if (s_apps[idx]->on_open != NULL) {
        s_apps[idx]->on_open(&s_instances[idx]);
    }
    return true;
}

void apps_request_open(const char *name)
{
    if (name == NULL) {
        return;
    }
    (void)snprintf(s_pending, sizeof(s_pending), "%s", name);
    atomic_store(&s_pending_valid, true);
}

void apps_update(void)
{
    if (atomic_exchange(&s_pending_valid, false)) {
        /* 拷贝到局部并立刻清空 s_pending，允许 on_open 内再次发起请求 */
        char name[APP_NAME_MAX];
        (void)strcpy(name, s_pending);
        s_pending[0] = '\0';
        (void)apps_open(name);
    }
    if (s_current >= 0 && s_apps[s_current]->on_running != NULL) {
        s_apps[s_current]->on_running(&s_instances[s_current]);
    }
}

const char *apps_current_name(void)
{
    return s_current >= 0 ? s_apps[s_current]->name : NULL;
}
