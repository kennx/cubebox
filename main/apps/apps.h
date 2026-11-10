#pragma once

#include <stdbool.h>
#include <stddef.h>

/* 注册表容量（同时安装的应用数上限） */
#define APPS_MAX 8

typedef struct app app_t;

/*
 * App 生命周期契约，对照 StackChan firmware/main/apps/app_template
 * （mooncake::AppAbility，https://github.com/m5stack/StackChan）：
 *
 *   apps_install ──> on_create ──> on_open ──> on_running(循环) ──> on_close
 *                     (安装,一次)   (打开)     (gui_task 每循环)    (被切换/关闭)
 *
 * 回调全部可选（填 NULL 跳过）。上下文约定：
 *  - on_create：安装期初始化，每应用只调用一次（重复 open 不会再触发）。
 *    由 app_main 安装时调用，可能在任意任务上下文，不得触碰 LVGL。
 *  - on_open：构建 UI、启动操作。仅 gui_task 上下文。
 *  - on_running：运行期间被 gui_task 主循环反复调用，做周期刷新/轮询。
 *    仅 gui_task 上下文；耗时操作会阻塞 UI，长任务自行建 FreeRTOS 任务。
 *  - on_close：销毁 UI、释放资源（user_data 置 NULL）。仅 gui_task 上下文。
 *
 * 生命周期回调内切换应用只允许 apps_request_open（下一轮 apps_update 生效）；
 * 禁止在回调内直接调用 apps_open——嵌套切换会在半初始化的应用上触发 on_close。
 */
typedef struct {
    const char *name;                    /* 唯一应用名（小写短词，如 "ok"） */
    void (*on_create)(app_t *app);       /* 安装（一次） */
    void (*on_open)(app_t *app);         /* 打开 */
    void (*on_running)(app_t *app);      /* 运行循环 */
    void (*on_close)(app_t *app);        /* 关闭 */
} app_lifecycle_t;

/* 应用实例：注册表内部持有；app 经 user_data 携带私有状态（如 view 句柄） */
struct app {
    const app_lifecycle_t *api;
    void *user_data;
};

/*
 * 安装应用（触发其 on_create）。返回 false：表满、重名、name 为空或 api 为空。
 * 注册表安装后视为只读：apps_install 只应在创建使用它的任务之前调用
 * （通常在 app_main），否则其他任务对注册表的无锁读存在竞态。
 */
bool apps_install(const app_lifecycle_t *api);

/*
 * 打开应用（先自动关闭当前应用）。仅 gui_task 上下文调用；未安装返回 false；
 * 重复打开同一应用为幂等 no-op。
 */
bool apps_open(const char *name);

/*
 * 请求切换应用：任意任务可调用（console/触摸等），由 gui_task 的 apps_update
 * 消费——这是非 gui_task 任务启动应用的唯一合法途径。
 * 经原子下标交接（注册表只读，无字符串拷贝，无撕裂/丢失）：未安装的应用名
 * 被忽略；回调内再次发起的请求在下一轮 apps_update 生效。
 */
void apps_request_open(const char *name);

/* gui_task 每主循环调用：先消费切换请求，再调用当前应用的 on_running */
void apps_update(void);

/* 当前打开的应用名；无应用打开时返回 NULL */
const char *apps_current_name(void);
