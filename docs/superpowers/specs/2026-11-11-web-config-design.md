# USB 网页配置器（CubeBox Web Configurator）设计

日期：2026-11-11
状态：定稿（builder 审查问题已裁决），待用户批准

## 意图与动机

设备目前只能通过 USB console 的三个调试命令（SNAP/NEXT/REBOOT）交互，Wi-Fi 凭据、
亮度、时区等设置无处安放。本设计建立一条「网页 ↔ USB ↔ 固件」的配置通道：

1. React 单页应用部署在 GitHub Pages（`gh-pages` 孤儿分支），界面现代、干净、直观（深色主题）。
2. 通过 Web Serial API 与设备的 USB-Serial/JTAG 口直连：未连接时提示插线并点按钮授权
   配对；配对成功进入配置界面。
3. 配置界面默认「通用配置」页：Wi-Fi（凭据）、屏幕亮度、时区；侧栏导航含其他 App
   配置入口（时钟等，本轮占位「即将上线」）。

## 目标

- 网页：连接页（授权/配对/握手）+ 配置页（通用配置 + App 占位导航）。
- 固件：console 新增 `PING` / `GETCFG` / `SETCFG` 三个命令，配置持久化到 NVS。
- 亮度、时区设置即时生效（亮度走 `hal_display_set_backlight`，时区 `setenv("TZ")+tzset()`
  —— `app_clock` 走 `localtime_r`，立即跟随）。
- Wi-Fi 凭据（SSID/密码）写入 NVS 保存，供未来 Wi-Fi 应用消费（本轮设备端无 Wi-Fi 栈）。
- 部署：`tools/deploy-web.sh` 一键构建并把 `web/dist` 提交到 `gh-pages` 孤儿分支。
- 网页带 `?mock=1` 设备模拟模式：无硬件也能走通全流程（开发/演示/自查用）。

## 非目标

- 设备端 Wi-Fi 连接与 SNTP 对时（用户已裁决：本轮只保存凭据，连接是独立后续工作）。
- PCF85063 RTC 硬件读写与对时——「时区」本轮作用于系统 TZ 环境变量。
- 时钟等 App 配置页内容（导航入口与占位页要有）。
- 固件 OTA、鉴权加密（USB 是物理信任通道，插线即授权）。
- GitHub Actions 自动部署（脚本部署已满足；Actions 以后可加）。

## 反目标（做得再对也算失败）

- 固件主循环/高频路径引入 malloc——配置操作是用户级低频事件，允许在命令处理内
  有限使用 cJSON（解析一次、立即释放）。
- 网页引入重型 UI 框架（MUI/AntD）——三张配置卡片的手写 CSS 足够，控制体积。
- 协议双向各有两套格式——请求/响应统一 JSON 行，`#` 前缀区分机器通道与日志噪声。

## 约束

- 浏览器：Web Serial 仅 Chromium 系（Chrome/Edge/Opera 87+）支持，且需 HTTPS——
  GitHub Pages 天然满足；连接页检测 `'serial' in navigator`，不支持时明确提示换浏览器。
- 串口通道共享：ESP_LOG、SNAP 截图流、配置协议同走 USB-Serial/JTAG；配置响应统一
  `#` 前缀（`#HELLO` / `#CFG` / `#ERR`），网页按行过滤解析，其余当噪声丢弃。
- USB VID 过滤：Espressif `0x303A`（ESP32-C6 USB-Serial/JTAG）。
- 固件架构：分层 `apps → hal → 驱动` 单向依赖；新配置模块落在 `main/` 根（系统服务，
  与组合根同级），依赖 hal 与 NVS/cJSON，不进 apps 层。
- NVS 分区已存在（partitions.csv），key 名 ≤15 字符。
- `main/CMakeLists.txt` GLOB 收源：新增 .c 后 `idf.py reconfigure`。

## 方案选择（已与用户确认 / 单项陈述）

- **Wi-Fi 范围**（用户选定）：只保存凭据到 NVS；`GETCFG` 返回 `wifi_pass_set`
  布尔而不回明文密码。
- **UI 基调**（用户选定）：深色主题（CSS 变量实现，后续可加浅色切换）。
- **协议格式：JSON 行**（否决 KV 行：嵌套字段 wifi {ssid,pass} 与未来 App 配置节
  扩展性差；否决二进制：无可测性收益，调试困难）。解析用 IDF 自带 cJSON 组件，
  生成用可主机测试的手写拼装（SSID 可含引号/反斜杠/UTF-8，转义逻辑必须可测）。
- **通信技术：Web Serial**（设备是 USB CDC 类设备，这是标准路径；WebUSB 不适用）。
- **持久化：NVS**（分区已有、断电可靠、key-value 模型天然匹配）。
- **部署：脚本 + gh-pages 孤儿分支**（用户指定建 gh-pages 分支；Actions 以后可加）。

## 组件设计

### 1. `main/config_model.{c,h}` —— 配置模型（纯逻辑，进主机测试）

无 IDF/LVGL 依赖，对照 `apps.c` / `dotmatrix.c` 的可测性分层：

```c
typedef struct {
    uint8_t brightness;          /* 0-100 */
    char    tz[40];              /* POSIX TZ 串，如 "CST-8" */
    char    wifi_ssid[33];
    char    wifi_pass[65];
} config_model_t;

void config_model_defaults(config_model_t *cfg);        /* brightness=100, tz="CST-8", wifi 空 */
/* 按 key 赋值并验证全部约束（见下）；失败返回 false 且不改动 cfg */
bool config_model_set_number(config_model_t *cfg, const char *key, double value);
bool config_model_set_string(config_model_t *cfg, const char *key, const char *value);
/* 生成 {"brightness":..,"tz":"..","wifi_ssid":"..","wifi_pass_set":true}，
   字符串经完整 JSON 转义（引号/反斜杠/控制字符/UTF-8 直出） */
void config_model_to_json(const config_model_t *cfg, char *buf, size_t len);
```

字段约束（`config_model_set_*` 的验证规则，测试逐条覆盖）：

| key | 类型 | 约束 |
|-----|------|------|
| `brightness` | number | 整数 0–100 |
| `tz` | string | 1–39 字符；字符白名单 `A-Za-z0-9 + - , . : / < >`（`.` 为 POSIX 夏令时规则 `M3.2.0/2` 必需） |
| `wifi_ssid` | string | 1–32 字符，可打印（0x20–0x7E） |
| `wifi_pass` | string | 0–64 字符，可打印；空串 = 清除已存密码 |

未知 key 一律忽略（向前兼容：网页新版发旧固件不认识的字段不报错）。

### 2. `main/config_store.{c,h}` —— 存储与应用（NVS + cJSON + 副作用）

```c
void config_store_init(void);   /* nvs_flash_init（含首次找不到分区时 erase 重试）→
                                   读 NVS 覆盖默认值 → 置 apply pending */
bool config_store_set_json(const char *json, char *err, size_t err_len); /* 解析→验证→
                                   NVS 写入 commit→置 apply pending；任一步失败填 err 返回 false */
void config_store_apply_pending(void);  /* 仅 gui_task 每轮调用：消费 pending，执行副作用 */
const char *config_store_get_json(char *buf, size_t buf_len);           /* 内部调 model 拼装 */
```

- NVS namespace `"cubebox"`，key：`bright`(u8) / `tz` / `wifi_ssid` / `wifi_pass`(str)。
- **副作用经 pending 标志 defer 到 gui_task 执行**（builder 审查裁决，对照 `s_snap_req`
  先例）：`hal_display_set_backlight()` 走 `esp_lcd_panel_io_tx_param`（SPI polling 事务），
  与 gui_task 的 `draw_bitmap`（queue 事务）并发不在驱动线程安全契约内；`tzset()` 修改
  libc 全局与 `localtime_r` 并发同理。故 SETCFG 在 console_task 只做「验证 → NVS commit →
  置 pending」，gui_task 每轮 `config_store_apply_pending()` 在 gui 上下文执行
  `setenv/tzset` 与 `set_backlight`——生效延迟 ≤10ms（一个循环周期），无感且零并发窗口。
- `config_store_init()` 由 main.c 在 `hal_display_init()` 之后、gui_task 创建之前调用：
  只 load + 置 pending，首次应用同样走 gui_task 首轮，路径统一。
- `SETCFG` 解析：`cJSON_Parse` → 遍历顶层对象 child，按 value 类型分派
  `config_model_set_number/string`（类型不匹配即失败）→ 全部通过后一次性写 NVS。
- main.c 现有硬编码 `setenv("TZ","CST-8")` 移除，由 config_store 接管。
- **CMake**：config_model.c / config_store.c 落 main/ 根，不被 hal/apps 的 GLOB 覆盖，
  须显式加入 `idf_component_register` 的 SRCS；REQUIRES 补 `nvs_flash`、`json`、`app_update`
  （`esp_app_get_description` 取固件版本）。

### 3. `main/main.c` console 协议扩展

行缓冲 16 → 512 字节，新增三命令（响应经 `usb_serial_jtag_write_bytes` 写出）：

```
PING            → #HELLO {"name":"CubeBox","fw":"<PROJECT_VER>","proto":1}
GETCFG          → #CFG {"brightness":100,"tz":"CST-8","wifi_ssid":"","wifi_pass_set":false}
SETCFG {json}   → 成功: #CFG {完整新状态}    失败: #ERR <原因短语>
```

- `fw` 取 `esp_app_get_description()->version`；顶层 CMakeLists 显式 `set(PROJECT_VER ...)`
  管理（初值 0.3.0）。
- 命令处理逻辑在 console_task 内保持直线分支（四命令各一行匹配 + JSON 委托
  config_store），预计 main.c 增量 ~40 行，不拆新文件。
- gui_task 主循环在 `apps_update()` 前加 `config_store_apply_pending()`（消费配置
  副作用，见组件 2 的 defer 裁决）。

### 4. `web/` —— React 工程（Vite + React + TypeScript）

```
web/
├── index.html  package.json  vite.config.ts  tsconfig.json  README.md
└── src/
    ├── main.tsx  App.tsx  styles.css        # 深色主题，CSS 变量集中定义
    ├── lib/device.ts                        # SerialDevice + MockDevice（同接口）
    └── components/
        ├── ConnectScreen.tsx                # 未连接：图标+提示+「授权并连接」按钮
        ├── ConfigScreen.tsx                 # 顶栏(状态点/固件版本/断开) + 侧栏导航 + 面板
        ├── WifiCard.tsx                     # SSID/密码输入 + 保存；回显 pass_set 状态
        ├── BrightnessCard.tsx               # 滑条 0-100%，松手(change)才发 SETCFG
        ├── TimezoneCard.tsx                 # 常用时区下拉(含夏令时规则)+自定义输入+保存
        └── PlaceholderCard.tsx              # App 配置占位「即将上线」
```

`lib/device.ts` 契约：

- `connect()`：`navigator.serial.requestPort({filters:[{usbVendorId:0x303A}]})` →
  `open({baudRate:115200})` → 发 `PING` → 3s 超时重试 2 次等 `#HELLO`，拿到即进入已连接。
- 读循环：`port.readable.getReader()` + `TextDecoder({stream:true})` 按 `\n` 分行，
  只处理 `#` 前缀行（`#HELLO`/`#CFG`/`#ERR` → tag + payload），其余丢弃。
- `getCfg()` / `setCfg(obj)`：请求-响应按到达顺序配对（单飞：进行中拒绝并发请求）。
- 保活：每 5s `PING`，连续 2 次无响应或 `navigator.serial` disconnect 事件 → 回未连接态。
- `MockDevice`：同接口，localStorage 存状态、150ms 模拟延迟；`?mock=1` 或
  浏览器不支持 Web Serial 时 dev 可用（生产 URL 带 mock 也允许，便于无设备演示）。
- UI 全中文；滑条拖动本地即时反馈、松手才提交；Wi-Fi 密码框可切换明文。

### 5. `tools/deploy-web.sh` —— gh-pages 部署

`cd web && npm ci && npm run build` → 首次 `git branch --orphan gh-pages`，经
`git worktree` 把 `dist/` 内容同步成 gh-pages 分支唯一内容 → 提交（`--push` 参数时
才推送）。`vite.config.ts` 设 `base:'/cubebox/'`（Pages 地址
`https://kennx.github.io/cubebox/`）。仓库 Settings → Pages 选 gh-pages 分支需用户
手动开启一次（部署文档说明）。

### 6. tests

- 新增 `tests/test_config_model.c`：默认值；四字段合法边界值（0/100、1/39 字符、
  32/64 字符）；非法值逐类拒绝（负数、101、超长、tz 非法字符、ssid 控制字符）；
  未知 key 忽略；`to_json` 转义（引号/反斜杠/\n/UTF-8 中文直出）与字段完整性；
  wifi_pass 空串语义。
- CMakeLists 加一条 target（沿用现有模式）。

### 7. 文档

- `docs/features/web-config.md`：协议规格（命令、JSON 字段、错误语义、并发/保活）、
  浏览器支持、部署步骤（Settings 开 Pages）。
- `web/README.md`：开发（npm i / dev / mock 模式）、构建、部署。
- `AGENTS.md`：项目地图补 `web/`；console 命令速记补 PING/GETCFG/SETCFG；验证要求
  补「改 web/ 后须 npm run build 通过」。

## 协议规格（proto 1，完整版进 docs/features/web-config.md）

- 传输：UTF-8 行文本，`\n` 结尾，与日志/截图流共通道，`#` 前缀行属协议。
- 网页 → 设备：`PING` / `GETCFG` / `SETCFG <json-object>`。
- 设备 → 网页：`#HELLO <json>` / `#CFG <json>` / `#ERR <text>`。
- 部分更新语义：SETCFG 只改出现的 key；类型不匹配或验证失败整体拒绝（不写 NVS）。

## 验证方式

1. 主机：`ctest`（含新 test_config_model）全绿。
2. 固件：`idf.py build` 零错误零新增警告。
3. 网页：`npm run build`（tsc + vite）通过；`?mock=1` 浏览器走查连接→配置→保存全流程。
4. 真机（设备在线 /dev/ttyACM0）：flash 后临时脚本冒烟——PING 握手、GETCFG 读回、
   SETCFG 改亮度（时钟页每秒走秒刷新与 SETCFG 并发期间无 SPI 错误日志，SNAP 截图
   对比亮度变化）、SETCFG 改时区（时钟页分钟跟随）、断电重启配置保持。
5. 部署：`tools/deploy-web.sh` 本地跑通，gh-pages 分支提交内容 = dist 且无多余文件。

## 风险与应对

| 风险 | 应对 |
|------|------|
| 串口被 idf.py monitor / screenshot.py 占用导致网页连不上 | 连接错误提示「检查端口占用」；文档写明互斥 |
| 浏览器不支持 Web Serial（Firefox/Safari） | 连接页能力检测 + 明确引导 Chrome/Edge |
| SETCFG 超长行截断产生残缺 JSON | 行缓冲 512B 覆盖最大合法载荷 ~130B；截断即 cJSON 解析失败 → #ERR |
| 页面开太久期间设备重启 | PING 保活 + serial disconnect 事件兜底回未连接态 |
| gh-pages 首次部署后 Pages 未开启 | 部署文档含 Settings 截图步骤说明 |

## 工作量预估

约 1 个工作日的 agent 实施（固件协议 + 网页 + 部署脚本）+ 真机联调。
