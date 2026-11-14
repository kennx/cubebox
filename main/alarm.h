#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * 闹钟纯逻辑：触发判定（模式/星期位/分钟去重）与渐入包络。
 * 无 IDF/LVGL 依赖，供 tests/ 主机端直接编译（分层对照 low_power.c）；
 * 响铃的动作执行（起停音频/切 app/唤醒背光）由 gui_task 胶水层完成（见 main.c）。
 */
typedef enum {
    ALARM_MODE_OFF = 0,   /* 关闭（默认，任何时刻不响） */
    ALARM_MODE_ONCE = 1,  /* 单次：下一个该 HH:MM 时刻响一次（触发后由 config_store 持久化关闭） */
    ALARM_MODE_WEEKLY = 2, /* 每周：仅 weekdays 选中的星期几响 */
    ALARM_MODE_DAILY = 3, /* 每天（永久）：每天该 HH:MM 响 */
} alarm_mode_t;

#define ALARM_RING_MS 180000u /* 响铃时长 3 分钟（用户需求，超时自动停） */
#define ALARM_FADE_MS 30000u  /* 渐入时长 30s：增益线性升至满幅 */

typedef struct {
    uint8_t mode;     /* alarm_mode_t */
    uint8_t hour;     /* 0-23 */
    uint8_t minute;   /* 0-59 */
    uint8_t weekdays; /* 位掩码 bit0=周一 … bit6=周日（仅每周模式生效） */
} alarm_cfg_t;

typedef struct {
    int64_t last_fired_key; /* 已触发分钟键（epoch 分钟号），<0 = 从未触发 */
} alarm_sched_ctx_t;

/** 复位调度上下文（gui_task 启动用：不记忆历史触发） */
void alarm_sched_reset(alarm_sched_ctx_t *ctx);

/*
 * 判定当前时刻是否触发（gui_task 每秒调用一次足够）：
 * local = 本地时间（HH:MM 与星期取自它），now_s = epoch 秒（分钟去重键）。
 * 触发条件：模式有效且非关 + HH:MM 匹配 +（每周模式当日位选中）+ 该分钟未触发过。
 * 返回 true 时 ctx 已记录该分钟（同分钟后续调用一律 false）。
 */
bool alarm_sched_check(const alarm_cfg_t *cfg, const struct tm *local, time_t now_s,
                       alarm_sched_ctx_t *ctx);

/*
 * 渐入增益（Q8 定点）：elapsed_ms ∈ [0,ALARM_FADE_MS) 线性 13→256（起步约 5%，
 * 前 30s 从轻到满），≥ALARM_FADE_MS 钳制 256。样本幅值 = 波形 × gain / 256。
 */
uint32_t alarm_fade_gain_q8(uint32_t elapsed_ms);
