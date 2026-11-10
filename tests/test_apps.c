/*
 * apps 框架主机端测试：注册表、生命周期回调顺序、切换与跨任务请求路径。
 * 直接编译 main/apps/apps.c（纯 C，无硬件/IDF 依赖）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/apps.h"

/* ---- 桩应用：记录各回调触发次数 ---- */

typedef struct {
    int create, open, running, close;
} counters_t;

static counters_t cnt_a, cnt_b, cnt_min;

static void a_on_create(app_t *app) { (void)app; cnt_a.create++; }
static void a_on_open(app_t *app) { (void)app; cnt_a.open++; }
static void a_on_running(app_t *app) { (void)app; cnt_a.running++; }
static void a_on_close(app_t *app) { (void)app; cnt_a.close++; }

static void b_on_create(app_t *app) { (void)app; cnt_b.create++; }
static void b_on_open(app_t *app) { (void)app; cnt_b.open++; }
static void b_on_running(app_t *app) { (void)app; cnt_b.running++; }
static void b_on_close(app_t *app) { (void)app; cnt_b.close++; }

static void min_on_create(app_t *app) { (void)app; cnt_min.create++; }

static const app_lifecycle_t app_stub_a = {
    .name = "a",
    .on_create = a_on_create,
    .on_open = a_on_open,
    .on_running = a_on_running,
    .on_close = a_on_close,
};

static const app_lifecycle_t app_stub_b = {
    .name = "b",
    .on_create = b_on_create,
    .on_open = b_on_open,
    .on_running = b_on_running,
    .on_close = b_on_close,
};

/* 除 on_create 外回调全 NULL 的最小应用：验证回调可选 */
static const app_lifecycle_t app_stub_min = {
    .name = "min",
    .on_create = min_on_create,
    .on_open = NULL,
    .on_running = NULL,
    .on_close = NULL,
};

/* ---- 断言 ---- */

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

static void expect_str(const char *actual, const char *expected, const char *label)
{
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n",
                label, expected, actual ? actual : "(null)");
        exit(1);
    }
}

int main(void)
{
    /* 安装：on_create 各触发一次；重名/NULL 拒绝 */
    expect(apps_install(&app_stub_a) == true, "install a");
    expect(apps_install(&app_stub_b) == true, "install b");
    expect(cnt_a.create == 1 && cnt_b.create == 1, "on_create once each at install");
    expect(apps_install(&app_stub_a) == false, "duplicate name rejected");
    expect(apps_install(NULL) == false, "NULL api rejected");

    /* 初始状态：无应用打开，update 不崩 */
    expect(apps_current_name() == NULL, "no app open initially");
    apps_update();
    expect(cnt_a.open == 0 && cnt_b.open == 0, "no on_open before open");

    /* 直接打开：on_open 一次；update 循环驱动 on_running */
    expect(apps_open("a") == true, "open a");
    expect(cnt_a.open == 1, "on_open once");
    expect_str(apps_current_name(), "a", "current is a");
    apps_update();
    apps_update();
    apps_update();
    expect(cnt_a.running == 3, "on_running per update");

    /* 幂等：重复 open 同一应用不重复 on_open/on_close */
    expect(apps_open("a") == true, "reopen a is idempotent");
    expect(cnt_a.open == 1 && cnt_a.close == 0, "no duplicate open/close");

    /* 未知应用：拒绝且状态不变 */
    expect(apps_open("nope") == false, "unknown app rejected");
    expect_str(apps_current_name(), "a", "still a after unknown");

    /* 跨任务请求：置请求后 current 不变，update 消费时才切换 */
    apps_request_open("b");
    expect_str(apps_current_name(), "a", "request does not switch immediately");
    apps_update();
    expect(cnt_a.close == 1, "on_close on switch out");
    expect(cnt_b.open == 1, "on_open on switch in");
    expect_str(apps_current_name(), "b", "current is b");
    expect(cnt_b.running == 1, "on_running after switch");
    apps_update();
    apps_update();
    expect(cnt_b.running == 3 && cnt_a.running == 3, "old app stops running");

    /* 最小应用（NULL 回调）：可安装可切换不崩 */
    expect(apps_install(&app_stub_min) == true, "install min");
    apps_request_open("min");
    apps_update();
    expect(cnt_b.close == 1, "b closed when min opens");
    expect_str(apps_current_name(), "min", "current is min");

    /* 回切：on_create 仍只有一次，on_open 再次触发 */
    apps_request_open("a");
    apps_update();
    expect(cnt_a.create == 1, "on_create still once across reopens");
    expect(cnt_a.open == 2, "on_open again on reopen");
    expect_str(apps_current_name(), "a", "back to a");

    printf("test_apps: all passed\n");
    return 0;
}
