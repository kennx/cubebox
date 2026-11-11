#pragma once

#include <stdbool.h>
#include <time.h>

/* 前向声明 IDF 句柄类型（与 driver/i2c_types.h 的定义逐字一致，同 hal_power.h 约定） */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

/*
 * PCF85063 RTC（I2C 0x51，AXP2101 电池备份掉电走时）。
 * 时间一律存 UTC；本地时间由 TZ 环境变量转换（app_clock 的 localtime_r）。
 * 初始化失败不致命：设备退化为仅靠 NTP 校时。
 */
void hal_rtc_init(i2c_master_bus_handle_t bus);

/* 读 RTC 到 *utc（struct tm 字段约定）；RTC 无效（时钟停振 / 年份越界）返回 false */
bool hal_rtc_get_time(struct tm *utc);

/* 写 RTC；失败返回 false */
bool hal_rtc_set_time(const struct tm *utc);
