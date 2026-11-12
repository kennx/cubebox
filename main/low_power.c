/*
 * 低功耗状态机实现：接口契约见 low_power.h。保持纯 C（无 IDF/LVGL 依赖），
 * 供 tests/ 直接编译；分层对照 apps.c / config_model.c 做法。
 */
#include "low_power.h"

void low_power_reset(low_power_ctx_t *ctx)
{
    ctx->dimmed = false;
    ctx->prev_idle_ms = 0;
    ctx->prev_key = false;
}

low_power_action_t low_power_step(low_power_ctx_t *ctx, uint32_t idle_ms,
                                  bool key_pressed, uint32_t timeout_s)
{
    const bool key_edge = key_pressed && !ctx->prev_key;
    const bool touch_activity = idle_ms < ctx->prev_idle_ms;
    low_power_action_t action = LOW_POWER_NONE;

    if (ctx->dimmed) {
        /* 唤醒：任意触摸活动、KEY 下降沿，或配置改为「永不」 */
        if (touch_activity || key_edge || timeout_s == 0) {
            ctx->dimmed = false;
            action = LOW_POWER_EXIT;
        }
    } else if (timeout_s != 0 && idle_ms >= timeout_s * 1000u) {
        ctx->dimmed = true;
        action = LOW_POWER_ENTER;
    }

    ctx->prev_idle_ms = idle_ms;
    ctx->prev_key = key_pressed;
    return action;
}
