#pragma once

#include <stdbool.h>

/* 前向声明 IDF 句柄类型（对照 hal_power.h），上层经 hal.h 聚合引入不传染驱动头 */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

/**
 * @brief 初始化音频输出（ES8311 + I2S，仅记总线引用；链路在首次响铃时懒建）
 * @param bus 共享 I2C 主总线句柄（由 main.c 创建；ES8311 @0x18）
 * @note 板载 ES8311 编解码（I2C 0x18）+ 功放，无独立 PA GPIO；I2S 引脚
 *       MCLK=19/BCLK=20/WS=22/DOUT=23。失败不致命（无扬声器时闹钟照常亮屏）。
 */
void hal_audio_init(i2c_master_bus_handle_t bus);

/**
 * @brief 开始响铃：懒建 I2S+codec 链路（16k/立体声/16bit）、音量设最大、
 *        起播放任务合成通用闹钟哔哔声（2000Hz 四短哔循环）。
 *        前 30s 增益线性渐入（alarm.h ALARM_FADE_MS / alarm_fade_gain_q8）。
 * @return false: 链路不可用（初始化失败/器件缺席/已在响铃中的重复调用除外——重复调用幂等 true）
 * @note 仅 gui_task 上下文调用（对照 hal_display 的 LVGL 上下文约束）
 */
bool hal_audio_alarm_start(void);

/**
 * @brief 停止响铃并整链拆除（codec close + I2S 通道删除，时钟停振省电）；
 *        等待播放任务排空当前块（≤ 一个 20ms 块）后返回。幂等。
 * @note 仅 gui_task 上下文调用
 */
void hal_audio_alarm_stop(void);
