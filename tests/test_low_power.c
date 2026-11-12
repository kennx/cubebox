/*
 * low_power 主机端测试：超时进入 / 触摸或 KEY 退出状态机（纯逻辑，无 IDF/LVGL 依赖）。
 * 直接编译 main/low_power.c。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "low_power.h"

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

int main(void)
{
    low_power_ctx_t ctx;

    /* ---- 初态：未超时无输入 → NONE，不置 dimmed ---- */
    low_power_reset(&ctx);
    expect(low_power_step(&ctx, 0, false, 30) == LOW_POWER_NONE, "initial idle 0 -> NONE");
    expect(!ctx.dimmed, "not dimmed initially");

    /* ---- 超时边界：29999ms 不进，30000ms 进（timeout 30s） ---- */
    low_power_reset(&ctx);
    expect(low_power_step(&ctx, 29999, false, 30) == LOW_POWER_NONE, "idle 29999ms -> NONE");
    expect(!ctx.dimmed, "not dimmed at 29999ms");
    expect(low_power_step(&ctx, 30000, false, 30) == LOW_POWER_ENTER, "idle 30000ms -> ENTER");
    expect(ctx.dimmed, "dimmed after ENTER");

    /* ---- ENTER 后 idle 持续增长：保持 dimmed，不再重复动作 ---- */
    expect(low_power_step(&ctx, 30010, false, 30) == LOW_POWER_NONE, "still dimmed -> NONE");
    expect(ctx.dimmed, "stays dimmed without activity");

    /* ---- dimmed 时触摸活动（idle 回落）→ EXIT，且只触发一次 ---- */
    expect(low_power_step(&ctx, 5, false, 30) == LOW_POWER_EXIT, "touch activity -> EXIT");
    expect(!ctx.dimmed, "awake after EXIT");
    expect(low_power_step(&ctx, 10, false, 30) == LOW_POWER_NONE, "after EXIT -> NONE (no repeat)");

    /* ---- dimmed 时 KEY 下降沿 → EXIT；按住不放（沿已过）不再动作。
     *      真实系统里 KEY 沿由胶水层 lv_disp_trig_activity 同帧重置 idle，按契约传入 */
    low_power_reset(&ctx);
    (void)low_power_step(&ctx, 30000, false, 30); /* ENTER */
    expect(low_power_step(&ctx, 5, true, 30) == LOW_POWER_EXIT, "key press edge (idle reset) -> EXIT");
    expect(!ctx.dimmed, "awake after key EXIT");
    expect(low_power_step(&ctx, 10, true, 30) == LOW_POWER_NONE,
           "key held (no new edge) no action");
    expect(low_power_step(&ctx, 20, false, 30) == LOW_POWER_NONE, "key release no action");
    expect(!ctx.dimmed, "still awake after held key");

    /* ---- 防御路径：KEY 沿单独出现（胶水层未重置 idle）也必须退出 dimmed ---- */
    low_power_reset(&ctx);
    (void)low_power_step(&ctx, 30000, false, 30); /* ENTER */
    expect(low_power_step(&ctx, 30010, true, 30) == LOW_POWER_EXIT, "key edge alone exits dimmed");

    /* ---- 清醒期 KEY 沿不单独阻止 ENTER：idle 未回落照样超时（KEY 活动由胶水层
     *      lv_disp_trig_activity 注入 idle 回落，纯层只认 idle/key 两种退出信号） ---- */
    low_power_reset(&ctx);
    expect(low_power_step(&ctx, 29999, true, 30) == LOW_POWER_NONE, "awake key press -> NONE");
    expect(low_power_step(&ctx, 30000, true, 30) == LOW_POWER_ENTER,
           "idle reached timeout despite held key -> ENTER");

    /* ---- timeout=0（永不）：idle 再大也不进入 ---- */
    low_power_reset(&ctx);
    expect(low_power_step(&ctx, 0, false, 0) == LOW_POWER_NONE, "never: idle 0 -> NONE");
    expect(low_power_step(&ctx, 3600000, false, 0) == LOW_POWER_NONE, "never: 1h idle -> NONE");
    expect(!ctx.dimmed, "never: not dimmed");

    /* ---- dimmed 期间 timeout 改为 0（永不）→ 退出低功耗 ---- */
    low_power_reset(&ctx);
    (void)low_power_step(&ctx, 30000, false, 30); /* ENTER */
    expect(low_power_step(&ctx, 30010, false, 0) == LOW_POWER_EXIT, "timeout->0 while dimmed -> EXIT");
    expect(!ctx.dimmed, "awake after timeout->0");

    /* ---- 唤醒后空闲计时自然重新累计：EXIT 后 idle 继续从低值增长再超时 ---- */
    low_power_reset(&ctx);
    (void)low_power_step(&ctx, 30000, false, 30); /* ENTER */
    (void)low_power_step(&ctx, 5, false, 30);      /* EXIT（触摸） */
    expect(low_power_step(&ctx, 10000, false, 30) == LOW_POWER_NONE, "re-accumulate: 10s -> NONE");
    expect(low_power_step(&ctx, 30000, false, 30) == LOW_POWER_ENTER, "re-accumulate: 30s -> ENTER");

    printf("test_low_power: all passed\n");
    return 0;
}
