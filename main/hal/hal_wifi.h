#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Wi-Fi STA 子系统：连接管理 + 附近 AP 扫描。
 * 凭据来自 config_store，经 config_store_apply_pending 幂等应用（gui_task 上下文）；
 * 断线重连由内部事件处理（凭据存在即无限重连，时钟设备常驻场景）。
 */
void hal_wifi_init(void);

/*
 * 幂等应用凭据：ssid 空 → 断开并清除目标；与当前目标相同 → 跳过；
 * 否则重配并连接。凭据长度上限与 esp_wifi 缓冲一致（ssid 32 / pass 64 字节）。
 */
void hal_wifi_apply_credentials(const char *ssid, const char *pass);

/*
 * 阻塞扫描附近未隐藏 AP（约 2-4 秒），按信号强度降序取前 16 条拼 JSON 数组：
 *   [{"ssid":"Home","rssi":-55,"secure":true},...]
 * 空结果返回 "[]"；busy / 失败返回 false（调用方回 #ERR）。
 */
bool hal_wifi_scan(char *buf, size_t buf_len);
