# USB 网页配置器（Web Configurator）

React 单页应用（`web/`，部署于 [gh-pages](https://kennx.github.io/cubebox/)）经 **Web Serial API**
直连设备的 USB-Serial/JTAG 口，完成配对与配置。本文是**协议与使用规格**；设计背景见
`docs/superpowers/specs/2026-11-11-web-config-design.md`。

## 用户流程

1. Chrome / Edge 打开配置页（Web Serial 仅 Chromium 系支持，且需 HTTPS——Pages 满足）。
2. USB 连接设备，点「授权并连接」→ 浏览器弹出设备选择（过滤 Espressif VID `0x303A`）→ 授权。
3. 配对成功进入「通用配置」：Wi-Fi 凭据 / 屏幕亮度 / 时区，保存即生效并持久化（断电保持）。
4. 侧栏「时钟」等 App 配置为占位，后续版本提供。

## 协议规格（proto 1）

传输层：UTF-8 行文本，`\n` 结尾，115200 baud。与 ESP_LOG、SNAP 截图流共用同一串口通道——
**协议响应行以 `#` 前缀标识**，其余行一律视为日志噪声丢弃。

### 网页 → 设备

| 命令 | 作用 |
|------|------|
| `PING` | 配对握手 / 保活探测 |
| `GETCFG` | 读全部通用配置 |
| `SETCFG <json>` | 部分更新配置（只改出现的 key） |

### 设备 → 网页

```
#HELLO {"name":"CubeBox","fw":"0.3.0","proto":1}     ← PING 的响应
#CFG   {"brightness":40,"tz":"CST-8","wifi_ssid":"Home","wifi_pass_set":true}
#ERR   <原因短语>                                     ← SETCFG 验证/解析/NVS 失败
```

`#CFG` 同时用作 `GETCFG` 的读响应与 `SETCFG` 的成功响应（返回完整新状态）。
**`wifi_pass` 永不回传**，只有派生布尔 `wifi_pass_set`。

### 字段约束（固件 `config_model` 强制，非法则整体拒绝且配置不变）

| key | 类型 | 约束 |
|-----|------|------|
| `brightness` | 整数 | 0–100（线性映射背光寄存器 0–255） |
| `tz` | 字符串 | 1–39 字节，白名单 `A-Za-z0-9 + - , . : / < >`（POSIX TZ，含夏令时规则如 `M3.2.0/2`） |
| `wifi_ssid` | 字符串 | 1–32 字节，可打印 ASCII（0x20–0x7E）或合法 UTF-8 序列（中文 SSID 支持；字节计长与 esp_wifi 缓冲一致） |
| `wifi_pass` | 字符串 | 0–64 字节，同上；空串 = 清除已存密码 |

未知 key 忽略（向前兼容：新版网页发旧固件不认识的字段不报错）；**已知 key 的 JSON
类型不匹配（如 `{"brightness":"40"}`）整体拒绝**，回 `#ERR bad field: <key>`。

### 语义与行为细节

- **部分更新**：`SETCFG {"brightness":40}` 只改亮度，其余保持。
- **原子性**：任一字段验证失败 → 整体拒绝（`#ERR bad field: <key>`），NVS 不写、内存态不变。
- **副作用延迟生效 ≤10ms**：SETCFG 在 console 任务只做「验证 → NVS commit → 置 pending」，
  gui_task 每轮消费 pending 执行亮度（SPI 并发安全）与时区（`setenv/tzset`，时钟立即跟随）。
- **保活与断开**：网页每 5s `PING`，连续 2 次无响应或收到浏览器 serial disconnect 事件即回到未连接页。
- **行长限制**：console 行缓冲 512B；USB-Serial/JTAG 驱动 RX 环 256B——单条命令请勿超过 ~200B
  （合法最大载荷 `SETCFG` 含 64B 密码约 130B，网页不会触界）；超长突发可能使行错乱、被当作未知命令静默忽略。
- 固件版本 `fw` 取自构建 `PROJECT_VER`（顶层 `CMakeLists.txt`）。

## 部署

```bash
tools/deploy-web.sh           # 构建 web/dist 并提交到 gh-pages 孤儿分支
tools/deploy-web.sh --push    # 同时推送 origin gh-pages
```

首次部署需在 GitHub 仓库 **Settings → Pages** 选 `gh-pages` 分支（/root），之后 push 即上线。
本地开发/无设备演示：`cd web && npm run dev`，`?mock=1` 启用 localStorage 模拟设备。
