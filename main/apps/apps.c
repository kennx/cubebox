/*
 * App 注册表与生命周期状态机。
 * 本文件刻意保持纯 C（不依赖 ESP-IDF/LVGL），供 tests/ 主机端测试直接编译
 * （tests/CMakeLists.txt，对照 StackChan firmware/tests 的做法）。
 */
#include <stdatomic.h>
#include <string.h>

#include "apps/apps.h"

static const app_lifecycle_t *s_apps[APPS_MAX];  /* 已安装应用的描述符 */
static app_t s_instances[APPS_MAX];              /* 各应用实例（api + user_data） */
static int s_current = -1;                       /* 当前打开的应用下标，-1 无 */

/* 跨任务切换请求：原子下标交接，-1 表示无请求。apps_request_open 先在只读
 * 注册表里把名字解析成下标，再原子写入——生产者/消费者之间没有任何共享
 * 字符串缓冲，天然无撕裂、无丢失。 */
static atomic_int s_pending_idx = -1;

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

/* 关闭当前应用并打开下标 idx 的应用（幂等）。仅 gui_task 上下文。 */
static void apps_open_idx(int idx)
{
    if (s_current == idx) {
        return; /* 幂等：已在打开 */
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
}

bool apps_open(const char *name)
{
    int idx = apps_find(name);
    if (idx < 0) {
        return false;
    }
    apps_open_idx(idx);
    return true;
}

void apps_request_open(const char *name)
{
    if (name == NULL) {
        return;
    }
    int idx = apps_find(name);
    if (idx >= 0) {
        atomic_store(&s_pending_idx, idx);
    }
}

void apps_update(void)
{
    /* exchange 一次性取走请求；on_open 内再次发起的请求留待下一轮消费 */
    int idx = atomic_exchange(&s_pending_idx, -1);
    if (idx >= 0) {
        apps_open_idx(idx);
    }
    if (s_current >= 0 && s_apps[s_current]->on_running != NULL) {
        s_apps[s_current]->on_running(&s_instances[s_current]);
    }
}

const char *apps_current_name(void)
{
    return s_current >= 0 ? s_apps[s_current]->name : NULL;
}

size_t apps_count(void)
{
    size_t n = 0;
    for (int i = 0; i < APPS_MAX; i++) {
        if (s_apps[i] != NULL) {
            n++;
        }
    }
    return n;
}

const char *apps_name_at(size_t idx)
{
    if (idx >= (size_t)APPS_MAX || s_apps[idx] == NULL) {
        return NULL;
    }
    return s_apps[idx]->name;
}
