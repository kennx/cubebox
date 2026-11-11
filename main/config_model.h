#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 设备通用配置模型：字段定义、默认值、按 key 验证赋值、JSON 拼装。
 * 纯逻辑模块（无 IDF/LVGL 依赖），供 tests/ 主机端直接编译。
 * 持久化（NVS）与副作用应用见 config_store；两者分层对照 apps.c 做法。
 */
typedef struct {
    uint8_t brightness;   /* 0-100，线性映射背光寄存器 */
    char    tz[40];       /* POSIX TZ 串，如 "CST-8" */
    char    wifi_ssid[33];
    char    wifi_pass[65];
    char    ntp_server[64]; /* NTP 服务器（域名/IPv4/IPv6），如 "ntp.aliyun.com" */
} config_model_t;

/* 填默认值：brightness=100、tz="CST-8"、Wi-Fi 空、ntp_server="ntp.aliyun.com" */
void config_model_defaults(config_model_t *cfg);

/* key 的期望值类型（SETCFG 类型分派用） */
typedef enum {
    CFG_TYPE_NUMBER,
    CFG_TYPE_STRING,
} config_model_type_t;

/** 已知 key 返回其期望类型；未知 key 返回 false */
bool config_model_key_type(const char *key, config_model_type_t *out);

/*
 * 按 key 赋数值字段（当前仅 "brightness"，整数 0-100）。
 * 已知字符串 key 传数值 → false（类型不匹配）；未知 key 返回 true（忽略）；
 * 验证失败返回 false 且不改动 cfg。
 */
bool config_model_set_number(config_model_t *cfg, const char *key, double value);

/*
 * 按 key 赋字符串字段并验证全部约束：
 *   tz        1-39 字节，白名单 A-Za-z0-9 + - , . : / < >（. 为 DST 规则必需）
 *   wifi_ssid 1-32 字节，可打印 ASCII（0x20-0x7E）或合法 UTF-8 序列
 *   wifi_pass 0-64 字节，同上（空串 = 清除已存密码；字节计长与 esp_wifi 缓冲一致）
 *   ntp_server 1-63 字节，白名单 A-Za-z0-9 . : -（域名 / IPv4 / IPv6）
 * 已知数值 key 传字符串 → false（类型不匹配）；未知 key 返回 true（忽略）；
 * 验证失败返回 false 且不改动 cfg。
 */
bool config_model_set_string(config_model_t *cfg, const char *key, const char *value);

/*
 * 生成 {"brightness":..,"tz":"..","wifi_ssid":"..","wifi_pass_set":bool,"ntp_server":".."}。
 * 字符串经完整 JSON 转义（引号/反斜杠/控制字符）；buf 不足时安全截断（NUL 结尾）。
 */
void config_model_to_json(const config_model_t *cfg, char *buf, size_t len);
