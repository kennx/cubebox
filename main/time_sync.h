#pragma once

#include <stdbool.h>

/*
 * 时间同步协调层：组合 config 的 ntp_server + hal_wifi + hal_rtc + SNTP。
 *   init      — 注册 SNTP 同步回调 + 从 RTC 恢复系统时间（AXP2101 电池备份，重启不丢）
 *   configure — 幂等配置 SNTP 服务器；联网后 lwip 自动周期校时（默认每小时）
 * SNTP 同步后：系统时钟由 lwip 自动 settimeofday（app_clock 的 time() 即刻正确），
 * 回调中另写回 RTC（存 UTC）供下次开机恢复。
 */
void time_sync_init(void);

/* 幂等：server 与当前配置相同则跳过；gui_task 上下文（apply_pending）调用 */
void time_sync_configure(const char *ntp_server);

/* 本次运行是否已通过 NTP 完成至少一次同步（TIME 命令用） */
bool time_sync_is_synced(void);
