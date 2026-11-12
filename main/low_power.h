#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 低功耗状态机：无操作超时 → 进入低功耗；触摸活动或 KEY 按下 → 退出。
 * 纯逻辑模块（无 IDF/LVGL 依赖），供 tests/ 主机端直接编译；动作的执行
 * （切时钟 app / 调背光）由 gui_task 胶水层完成（见 main.c）。
 *
 * 语义：
 *  - 触摸活动 = 本轮 idle_ms 小于上一轮（LVGL 无操作时长被输入重置而回落）。
 *    KEY 属于活动由胶水层经 lv_disp_trig_activity 注入 idle 回落实现；
 *    DIMMED 态下 KEY 下降沿在本层直接视为唤醒信号。
 *  - timeout_s == 0 表示永不：清醒期不进入；低功耗期视为「配置已改为永不」→ 退出。
 *  - ENTER/EXIT 仅在状态转变的那一轮返回，不重复。
 */
typedef enum {
    LOW_POWER_NONE = 0,
    LOW_POWER_ENTER, /* 进入低功耗：切回时钟 app + 降亮度 */
    LOW_POWER_EXIT,  /* 唤醒：恢复配置亮度 */
} low_power_action_t;

typedef struct {
    bool dimmed;          /* 当前是否处于低功耗态 */
    uint32_t prev_idle_ms; /* 上一轮无操作时长（检测回落） */
    bool prev_key;        /* 上一轮 KEY 电平（检测下降沿） */
} low_power_ctx_t;

/** 初始化/复位为清醒态（gui_task 启动或重启计时用） */
void low_power_reset(low_power_ctx_t *ctx);

/**
 * 推进一轮状态机。idle_ms = LVGL 无操作时长；key_pressed = KEY 当前按下；
 * timeout_s = 配置的超时秒数（0=永不）。返回本轮动作。
 */
low_power_action_t low_power_step(low_power_ctx_t *ctx, uint32_t idle_ms,
                                  bool key_pressed, uint32_t timeout_s);
