/*
 * 闹钟纯逻辑实现。分钟去重键用 epoch 分钟号（now_s/60）：跨天/跨年天然唯一，
 * 无需 tm_yday 换算；判定本身只依赖 local 的 HH:MM 与星期。
 * 本文件保持纯 C（无 IDF/LVGL 依赖），供 tests/ 直接编译。
 */
#include "alarm.h"

void alarm_sched_reset(alarm_sched_ctx_t *ctx)
{
    ctx->last_fired_key = -1;
}

bool alarm_sched_check(const alarm_cfg_t *cfg, const struct tm *local, time_t now_s,
                       alarm_sched_ctx_t *ctx)
{
    if (cfg == NULL || local == NULL || ctx == NULL) {
        return false;
    }
    if (cfg->mode < ALARM_MODE_ONCE || cfg->mode > ALARM_MODE_DAILY) {
        return false; /* 关闭与越界（久置 NVS 脏值）一律不响 */
    }
    if (local->tm_hour != cfg->hour || local->tm_min != cfg->minute) {
        return false;
    }
    if (cfg->mode == ALARM_MODE_WEEKLY) {
        /* tm_wday：0=周日…6=周六 → 周一位 bit0…周日位 bit6 */
        int bit = (local->tm_wday + 6) % 7;
        if ((cfg->weekdays & (1u << bit)) == 0) {
            return false;
        }
    }

    int64_t key = (int64_t)(now_s / 60);
    if (key == ctx->last_fired_key) {
        return false; /* 该分钟已触发过（同分钟轮询/循环模式防重复响） */
    }
    ctx->last_fired_key = key;
    return true;
}

uint32_t alarm_fade_gain_q8(uint32_t elapsed_ms)
{
    const uint32_t start = 13;   /* 起步 ≈5%，避免完全无声而错过闹钟 */
    const uint32_t full = 256;   /* Q8 满幅 */
    if (elapsed_ms >= ALARM_FADE_MS) {
        return full;
    }
    return start + (full - start) * elapsed_ms / ALARM_FADE_MS;
}
