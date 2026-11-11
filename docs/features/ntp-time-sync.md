# NTP 时间同步与 RTC

设备时钟（`app_clock` 读系统时钟 `time()` + `localtime_r` 按 TZ 换算）的时间来源链：

```
开机：PCF85063 RTC ──(hal_rtc_get_time)──> settimeofday ──> 系统时钟
联网：Wi-Fi STA（config 的 wifi_ssid/wifi_pass）──> SNTP(ntp_server) ──┐
                                            自动 settimeofday + 写回 RTC ─┘
```

## 组成

| 模块 | 职责 |
|------|------|
| `main/hal/hal_rtc.{c,h}` | PCF85063（I2C 0x51，`waveshare/pcf85063a` 组件）读写封装；存 **UTC**；秒寄存器 OS 位/年份 <2020 视为无效；初始化失败不致命 |
| `main/hal/hal_wifi.{c,h}` | Wi-Fi STA：init 启动不连接；凭据经 `hal_wifi_apply_credentials` **幂等**应用（相同跳过）；断线事件自动重连（凭据存在即无限重连）；`hal_wifi_scan` 阻塞扫描输出 JSON |
| `main/time_sync.{c,h}` | 协调层：开机 RTC → 系统时间恢复（手写 tm→epoch，newlib 无 timegm）；SNTP 服务器**幂等**配置（lwip `setservername` 只存指针，用永久静态缓冲）；同步回调写回 RTC + `synced` 标志 |
| `config_model` 的 `ntp_server` | 1–63 字节，白名单 `A-Za-z0-9.:-`（域名/IPv4/IPv6），默认 `ntp.aliyun.com`，NVS key `ntp_server` |

## 生效路径

配置副作用统一走 `config_store_apply_pending()`（gui_task 上下文，开机首轮与每次 SETCFG 同一路径）：

```c
hal_wifi_apply_credentials(s_cfg.wifi_ssid, s_cfg.wifi_pass);  /* ssid 空 = 断开 */
time_sync_configure(s_cfg.ntp_server);
```

- 未配置 Wi-Fi 时设备不联网，系统时间仅靠 RTC 走时（首次使用 RTC 无效则待 NTP）。
- SNTP 由 lwip 周期轮询（默认每小时重同步），DNS 失败静默重试。
- `synced`（`TIME` 命令 / 网页时钟页）语义为「本次运行已完成至少一次 NTP 同步」。

## 验证

- 主机端：`ctest`（`ntp_server` 字段验证用例，见 `tests/test_config_model.c`）。
- 上机：`SETCFG {"wifi_ssid":..,"wifi_pass":..}` 后串口观察连接与 `NTP synced, RTC updated` 日志；
  `TIME` 确认 `synced:true` 且 local 正确；`REBOOT` 后 `TIME` 时间仍正确（RTC 电池保持）；
  `SNAP` 截图确认时钟 app 显示正确；`SCAN` 返回附近 AP 列表。
