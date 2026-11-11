# USB 网页配置器 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立「网页(Web Serial) ↔ USB console ↔ 固件 NVS」配置通道：React 配置器部署 gh-pages，固件新增 PING/GETCFG/SETCFG 协议。

**Architecture:** 固件侧 `config_model`（纯逻辑、主机测试）+ `config_store`（NVS/cJSON/副作用 defer 到 gui_task）+ main.c console 扩展；网页侧 `web/` Vite+React+TS，`lib/device.ts` 抽象真机（Web Serial）与 mock 双实现；`tools/deploy-web.sh` 把构建产物提交到 gh-pages 孤儿分支。

**Tech Stack:** ESP-IDF v5.5（cJSON=json 组件、nvs_flash、app_update）、CMake+ctest、Vite 7 + React 19 + TypeScript（零 UI 框架，手写 CSS）。

**Spec:** `docs/superpowers/specs/2026-11-11-web-config-design.md`（本计划从该规格推导，执行者须同时阅读）

## Global Constraints

- C 代码遵循 ESP-IDF 风格（K&R、4 空格、snake_case 函数、`s_` 静态、`#pragma once`）。
- 提交标题用 Conventional Commits；正文按文件分组说明改了什么为什么。
- 主机测试命令：`cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`（编译带 `-Wall -Werror` + asan/ubsan）。
- 固件构建命令（需先 `export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.5_py3.12_env && source ~/esp/esp-idf/export.sh`）：`idf.py build` 零错误零新增警告；新增 .c 在 main/ 根不被 GLOB 覆盖，须显式进 SRCS 并 `idf.py reconfigure`。
- 网页 UI 文案全中文；构建验证 `cd web && npm run build`（tsc 严格模式 + vite）。
- 协议：UTF-8 行文本 `\n` 结尾；设备响应行 `#` 前缀（`#HELLO`/`#CFG`/`#ERR`），其余行是日志噪声；串口 115200；VID 0x303A。
- 深色主题色板（styles.css `:root`）：`--bg:#0f1115 --panel:#171a21 --border:#262b36 --text:#e8eaf0 --muted:#8b93a5 --accent:#4f8cff --ok:#3ecf8e --err:#ff6b6b`。
- 设备在线 `/dev/ttyACM0`；截图验证用 `/usr/bin/python3 tools/screenshot.py`（IDF venv 缺 Pillow）；串口脚本用 IDF venv 的 python3（有 pyserial）。
- `sdkconfig` 已被 .gitignore 忽略，改动 sdkconfig 不入提交。

## Review Focus

1. **SSID/密码含 `"`、`\`、UTF-8 中文**：JSON 拼装与解析两端往返不坏——Task 1 转义测试 + Task 2 真机 set→get 回环冒烟。
2. **含夏令时规则的 tz**（`M3.2.0/2`、`M10.5.0/3`）必须接受；空串/超长/非法字符（空格、`;`、`!`）拒绝——Task 1 边界测试。
3. **SETCFG 部分字段非法**：整体拒绝，NVS 不写、内存态不换（不得半应用）——Task 1 模型层测试 + Task 2 冒烟（`#ERR` 后 GETCFG 读回旧值）。
4. **超长/残缺 JSON 行**：设备回 `#ERR` 不崩溃、后续命令仍可用——Task 2 冒烟（发 600 字节行与 `SETCFG {`）。
5. **设备中途拔线/重启**：网页不卡死、回未连接态可重连——Task 4 mock 走查断开路径 + Task 2 真机 REBOOT 期间网页表现（手测项，列入验证清单）。

---

### Task 0: 提交设计文档与本计划

**Files:**
- Create: `docs/superpowers/plans/2026-11-11-web-config.md`（本文件）
- Modify: `docs/superpowers/specs/2026-11-11-web-config-design.md`（已定稿）

- [ ] **Step 1: Commit**

```bash
git add docs/superpowers/specs/2026-11-11-web-config-design.md docs/superpowers/plans/2026-11-11-web-config.md
git commit -m "docs: 网页配置器设计定稿与实施计划"
```

---

### Task 1: config_model 纯逻辑模块 + 主机测试（TDD）

**Files:**
- Create: `main/config_model.h`、`main/config_model.c`
- Create: `tests/test_config_model.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces（后续任务依赖的确切签名）:

```c
typedef struct {
    uint8_t brightness;   /* 0-100 */
    char tz[40];          /* POSIX TZ */
    char wifi_ssid[33];
    char wifi_pass[65];
} config_model_t;

void config_model_defaults(config_model_t *cfg);            /* brightness=100, tz="CST-8", ssid/pass 空 */
bool config_model_set_number(config_model_t *cfg, const char *key, double value);
bool config_model_set_string(config_model_t *cfg, const char *key, const char *value);
void config_model_to_json(const config_model_t *cfg, char *buf, size_t len);
```

语义：set_* 验证失败返回 false 且不改动 cfg；未知 key 返回 true（忽略）；`brightness` 要求整数 0–100；tz 1–39 字符、白名单 `A-Za-z0-9 + - , . : / < >`；wifi_ssid 1–32 可打印字符（0x20–0x7E）；wifi_pass 0–64 可打印（空串=清除）。`to_json` 输出 `{"brightness":100,"tz":"CST-8","wifi_ssid":"","wifi_pass_set":false}`，字符串走完整 JSON 转义（`"`→`\"`、`\`→`\\`、控制字符→`\u00XX`、UTF-8 原样直出），截断安全（sntruncate 不越界）。

- [ ] **Step 1: 写失败测试 `tests/test_config_model.c`**（沿用 test_apps.c 的 `expect`/`expect_str` 风格，断言逐条对应 Interfaces 语义）：
  - defaults 初值四字段；set_number("brightness") 0 与 100 边界过、-1/101/80.5 拒；set_number 未知 key "foo" 过且不改 cfg。
  - set_string("tz")：`"CST-8"`、`"GMT0BST,M3.5.0/1,M10.5.0"`、`"AEST-10AEDT,M10.1.0,M4.1.0/3"` 过；空串、40 字符、含空格/`;`/`!` 拒。
  - set_string("wifi_ssid")：32 字符过、33 拒、含 `"` 与 `\` 与中文「家」拒（可打印约束）、空串拒。
  - set_string("wifi_pass")：64 字符过、65 拒、空串过（=清除）。
  - 验证失败不改 cfg：先合法 set，再非法 set，断言原值仍在。
  - to_json：默认值串精确匹配；ssid=`a"b\c` 时输出含 `a\"b\\c`；含 `\n`(0x0A) 的 pass（不可打印→拒，改测 tz 无需——控制字符路径用直接调内部转义的坏例不可行，改为：pass=`x`，手工构造含 0x01 的结构体字段后 to_json 输出含 `\u0001`）；中文 ssid 拒，改在 GETCFG 回环（Task 2）验证 UTF-8 直出——用 tz 白名单不含中文故此路径单测以 `\u0001` 覆盖控制字符转义。
  - buf 不足时截断不越界：len=8 的 buf 调 to_json 后 buf 是 7 字符 + NUL（配合 asan）。
- [ ] **Step 2: tests/CMakeLists.txt 加 `test_config_model` target**（照抄 test_dotmatrix 块：源 `test_config_model.c ../main/config_model.c`，同 flags，同 include）。
- [ ] **Step 3: 跑测试确认失败**

Run: `cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`
Expected: 编译失败（config_model.h 不存在）或链接失败。

- [ ] **Step 4: 实现 `main/config_model.{c,h}`**——零 IDF/LVGL 依赖（对照 apps.c）；`to_json` 手写拼装 + 转义函数；`set_number` 的整数判定 `value == (double)(long long)value`。
- [ ] **Step 5: 跑测试确认全过**（同 Step 3 命令，Expected: 3 tests Passed）。
- [ ] **Step 6: Commit**

```bash
git add main/config_model.h main/config_model.c tests/test_config_model.c tests/CMakeLists.txt
git commit -m "feat: 配置模型 config_model——字段验证与 JSON 拼装（纯逻辑，主机测试）"
```

---

### Task 2: config_store + console 协议 + main.c 集成

**Files:**
- Create: `main/config_store.h`、`main/config_store.c`
- Modify: `main/main.c`、`main/CMakeLists.txt`、`CMakeLists.txt`（顶层）

**Interfaces:**
- Consumes: Task 1 的 `config_model_*` 全套；`hal_display_set_backlight(uint8_t)`（main/hal/hal_display.h）。
- Produces:

```c
void config_store_init(void);      /* nvs init → 读 NVS 覆盖默认值 → 置 pending；app_main 在 hal_display_init 后调用 */
bool config_store_set_json(const char *json, char *err, size_t err_len); /* console_task 上下文 */
void config_store_apply_pending(void); /* 仅 gui_task 每轮调用 */
const char *config_store_get_json(char *buf, size_t buf_len);           /* 返回 buf，内容同 to_json */
```

`set_json` 流程（原子性关键）：`config_model_t tmp = s_cfg`（副本）→ cJSON_Parse 失败填 err="parse error" → 遍历顶层 child 按 cJSON type 分派 `config_model_set_number/string`（任一失败填 err="bad field: <key>" 并整体放弃）→ 全过则 NVS 写 4 key + commit（失败填 err="nvs error"）→ `s_cfg = tmp` → `s_apply_pending = true`。pending 标志 `static volatile bool`（console 置/gui 清，单字节读写天然原子，对照 `s_snap_req` 先例）；`apply_pending` 执行 `setenv("TZ")+tzset()+hal_display_set_backlight(s_cfg.brightness)`。

main.c：console 行缓冲 `char line[16]`→`char line[512]`，console 任务栈 4096→6144（两个 512B 局部缓冲）；新增分支 `PING`（回 `#HELLO {"name":"CubeBox","fw":"<esp_app_get_description()->version>","proto":1}`）、`GETCFG`（回 `#CFG <json>`）、`strncmp(line,"SETCFG ",7)==0`（成功回 `#CFG <json>`、失败回 `#ERR <短语>`）；gui_task 循环在 `apps_update()` 前加 `config_store_apply_pending()`；app_main 移除硬编码 `setenv("TZ","CST-8")` 块，在 `hal_display_init()` 后加 `config_store_init()`。

main/CMakeLists.txt：SRCS 显式追加 `config_model.c config_store.c`；REQUIRES 追加 `nvs_flash json app_update`。顶层 CMakeLists.txt：`project(cubebox)` 前加 `set(PROJECT_VER "0.3.0")`。

- [ ] **Step 1: 实现 `main/config_store.{c,h}` 与全部集成改动**（无主机测试可写——NVS/cJSON 依赖 IDF；行为验证在本任务真机冒烟与 Task 6 回归）。
- [ ] **Step 2: 构建验证**——`idf.py reconfigure && idf.py build`，Expected: 零错误零新增警告。
- [ ] **Step 3: 烧录**——`tools/flash.sh`（设备 /dev/ttyACM0）。
- [ ] **Step 4: 真机协议冒烟**——IDF venv python3 一次性脚本（不入仓库）：开 115200 读线程滤 `#` 行——① 发 `PING` 等 `#HELLO`（含 "CubeBox" 与 fw 版本）；② `GETCFG` 得默认配置；③ `SETCFG {"brightness":40}` 后 `GETCFG` 读回 brightness=40，同时观察屏幕变暗；④ `SETCFG {"tz":"JST-9"}` 读回成功；⑤ `SETCFG {"brightness":200}` 得 `#ERR`，随后 `GETCFG` 仍 40（原子性）；⑥ 发 600 字节垃圾行 + `SETCFG {`，各得 `#ERR` 或无响应，随后 `PING` 仍通；⑦ `REBOOT` 后 `GETCFG` 保持 brightness=40（NVS 持久）。
- [ ] **Step 5: SNAP 截图对比亮度**——`SNAP` + `/usr/bin/python3 tools/screenshot.py`，40% 亮度下时钟点阵明显暗于 100%（像素均值对比），无 SPI 错误日志（走秒刷新与 SETCFG 并发窗口已由 defer 消除，日志中不出现 Backlight/LVGL flush 报错）。
- [ ] **Step 6: Commit**

```bash
git add main/config_store.h main/config_store.c main/main.c main/CMakeLists.txt CMakeLists.txt
git commit -m "feat: USB 配置协议——config_store(NVS 持久化) + PING/GETCFG/SETCFG，副作用 defer 到 gui_task"
```

---

### Task 3: web 工程脚手架 + 设备通信层（Web Serial + mock）

**Files:**
- Create: `web/`（package.json、vite.config.ts、tsconfig.json、index.html、src/main.tsx、src/App.tsx 占位、src/lib/device.ts）
- Modify: `.gitignore`（加 `node_modules/`）

**Interfaces:**
- Consumes: Task 2 协议（命令/响应格式见 Global Constraints）。
- Produces（Task 4 依赖）:

```ts
export interface DeviceInfo { name: string; fw: string; proto: number }
export interface DeviceConfig { brightness: number; tz: string; wifi_ssid: string; wifi_pass_set: boolean }
export interface Device {
  connect(): Promise<DeviceInfo>;        // 授权+开串口+PING 握手（3s 超时重试 2 次）
  disconnect(): Promise<void>;
  getCfg(): Promise<DeviceConfig>;
  setCfg(patch: { brightness?: number; tz?: string; wifi_ssid?: string; wifi_pass?: string }): Promise<DeviceConfig>;
  onDisconnect(cb: () => void): void;    // 保活失败 2 次（5s 间隔 PING）或 serial disconnect 事件
}
export function createDevice(): Device;  // location.search 含 mock=1 → MockDevice；否则 SerialDevice（不支持 Web Serial 时 connect() 抛特定错误）
```

SerialDevice 实现要点：`navigator.serial.requestPort({filters:[{usbVendorId:0x303A}]})` → `open({baudRate:115200})`；读循环 `port.readable.getReader()` + `TextDecoder({stream:true})` 按 `\n` 分行，仅 `#` 前缀行解析（`#HELLO`/`#CFG`/`#ERR` → tag+payload），`#ERR` 使当前在途请求 reject；请求-响应单飞（在途时新请求排队）；`PING` 保活定时器。MockDevice：localStorage key `cubebox.mock.cfg` 存配置 JSON，150ms setTimeout 模拟延迟，非法字段模拟 `#ERR`（亮度 0-100、tz 白名单同固件）。

- [ ] **Step 1: 写 `web/` 工程文件**（手写全部文件，不跑交互式脚手架；依赖 `react/react-dom ^19` + dev `vite ^7 @vitejs/plugin-react ^5 typescript ~5.9 @types/react @types/react-dom`；`vite.config.ts` 此时先不设 base，Task 5 再加；tsconfig 用 Vite 官方 react-ts 模板的严格配置；index.html `lang="zh-CN"` title「CubeBox 配置」）。App.tsx 暂为调用 `createDevice()` 后把 `getCfg()` 结果 `<pre>` 打印的最小页。
- [ ] **Step 2: `.gitignore` 追加 `node_modules/`，`cd web && npm install && npm run build`**，Expected: tsc+vite 构建通过。
- [ ] **Step 3: mock 走查**——`npm run dev` 起服务，`?mock=1` 打开，页面打印出 mock 默认配置；控制台无错误。
- [ ] **Step 4: 真机走查（可选，设备在时）**——不带 mock 打开 dev 页，点连接（需临时按钮或直接调），确认握手拿到 fw 版本。若浏览器环境无串口权限则跳过（Task 4 完整验证）。
- [ ] **Step 5: Commit**

```bash
git add web/ .gitignore
git commit -m "feat: web 工程——Vite+React+TS 与设备通信层（Web Serial + mock 双实现）"
```

---

### Task 4: 配置界面（连接页 + 配置页 + 卡片 + 深色样式）

**Files:**
- Create: `web/src/styles.css`、`web/src/components/ConnectScreen.tsx`、`ConfigScreen.tsx`、`WifiCard.tsx`、`BrightnessCard.tsx`、`TimezoneCard.tsx`、`PlaceholderCard.tsx`
- Modify: `web/src/App.tsx`

**Interfaces:**
- Consumes: Task 3 的 `Device/DeviceInfo/DeviceConfig/createDevice`。
- Produces: 完整 UI（布局见 spec 组件 4；深色色板见 Global Constraints）。

组件契约：`App` 持 `device` 与 `DeviceInfo`，未连接渲染 `ConnectScreen`（props: `onConnect: (d: Device, info: DeviceInfo) => void`），连接后渲染 `ConfigScreen`（props: `device, info, onDisconnect`）。`ConfigScreen` 顶栏（状态点+设备名+固件版本+断开按钮）+ 左侧栏（「通用配置」高亮、「时钟」置灰标「即将上线」）+ 右面板。面板初次进入 `getCfg()` 装载；三张卡片：

- `WifiCard`（props: `cfg: DeviceConfig, onSave: (ssid, pass) => Promise<void>`）：SSID 输入（初值 cfg.wifi_ssid）、密码输入（可切换明文、placeholder 显示「已保存，留空保持不变」当 wifi_pass_set）、保存按钮 + 已保存状态徽标；ssid 留空时保存禁用。
- `BrightnessCard`（props: `brightness: number, onChange: (v: number) => Promise<void>`）：滑条 0-100 显示百分比，拖动本地即时反馈、`change` 事件（松手）才提交。
- `TimezoneCard`（props: `tz: string, onSave: (tz: string) => Promise<void>`）：下拉常用时区（选项 value 为 POSIX 串：北京 `CST-8`、香港 `HKT-8`、东京 `JST-9`、曼谷 `WIB-7`、新德里 `IST-5:30`、莫斯科 `MSK-3`、东欧 `EET-2EET,M3.5.0/3,M10.5.0/4`、中欧 `CET-1CEST,M3.5.0,M10.5.0/3`、伦敦 `GMT0BST,M3.5.0/1,M10.5.0`、悉尼 `AEST-10AEDT,M10.1.0,M4.1.0/3`、奥克兰 `NZST-12NZDT,M9.5.0,M4.1.0/3`、纽约 `EST5EDT,M3.2.0,M11.1.0`、洛杉矶 `PST8PDT,M3.2.0,M11.1.0`、UTC `UTC0`）+「自定义…」项展开文本框；当前 tz 不在列表时自动落到自定义项；保存按钮。
- `PlaceholderCard`（props: `title: string`）：图标 + 「即将上线」。

`ConnectScreen` 文案：主标题「CubeBox 配置」、说明「使用 USB 线将设备连接到电脑，然后授权连接」、按钮「授权并连接」；`'serial' in navigator` 为假时显示「当前浏览器不支持 Web Serial，请使用 Chrome 或 Edge 打开」并禁用按钮；连接中显示等待态，失败显示可重试错误（含「串口可能被其他程序占用」提示）。

- [ ] **Step 1: 实现 styles.css 与全部组件，重写 App.tsx 状态机**（disconnected/connecting/connected；`onDisconnect` 回调触发时回未连接态）。
- [ ] **Step 2: `npm run build`**，Expected: 通过。
- [ ] **Step 3: 浏览器走查（mock）**——dev server `?mock=1`：连接页渲染 → 授权连接 → 配置页三卡片显示 mock 配置 → 拖滑条松手后 mock 状态更新 → Wi-Fi 保存 ssid=`a"b\c` 中文备注不崩溃 → 时区切「东京」保存 → mock 刷新后（localStorage）配置保持 → 断开回连接页。用 browser-use 截图（连接页/配置页各一张）留档。
- [ ] **Step 4: 真机走查**——真机 dev 页连接 → 拖亮度滑条设备屏幕同步变暗/变亮 → 保存时区后时钟页分钟显示跟随（若当分钟跨时区可见差）→ 断开/拔线（手测）回连接页无卡死。
- [ ] **Step 5: Commit**

```bash
git add web/
git commit -m "feat: 配置界面——连接配对页与通用配置（Wi-Fi/亮度/时区），深色主题"
```

---

### Task 5: gh-pages 部署

**Files:**
- Create: `tools/deploy-web.sh`（可执行）、`web/README.md`
- Modify: `web/vite.config.ts`（加 `base: '/cubebox/'`）、`.gitignore` 无需再改（`dist/` 已全局忽略）

**Interfaces:**
- Consumes: Task 3/4 的 web 工程。
- Produces: 本地 `gh-pages` 孤儿分支（内容=dist）；`--push` 时才推送。

`deploy-web.sh` 逻辑（bash，`set -euo pipefail`）：`cd web && npm ci && npm run build` → 校验 `web/dist/index.html` 存在 → `tmp=$(mktemp -d)` → `git worktree add --detach "$tmp" HEAD` → 在 worktree 内 `git checkout --orphan gh-pages`（分支已存在则 `git checkout gh-pages`）→ `git rm -rf --quiet .` 清空既有内容（首次 orphan 清的是 HEAD 残留、复跑清的是旧部署）→ `cp -r "$ROOT/web/dist/"* .` → `git add -A` → `git commit -m "deploy: 网页配置器 $(date +%F)"`（无变更则提示已最新退出 0）→ 清理 worktree（`git worktree remove --force`）→ `--push` 参数时 `git push origin gh-pages`。`web/README.md`：开发（npm install/dev/?mock=1）、构建、部署（含「仓库 Settings → Pages 选 gh-pages 分支 /root」一次性手动步骤说明）。

- [ ] **Step 1: 实现脚本与 README，vite 加 base。**
- [ ] **Step 2: 跑 `tools/deploy-web.sh`**——Expected: `git log gh-pages -1` 是 deploy 提交、`git ls-tree gh-pages` 仅 index.html+assets/（无 node_modules/源码）；工作区仍在 main 未被污染。
- [ ] **Step 3: 二次运行幂等**——再跑一次，Expected: 「已最新」退出 0 或新提交成功，无报错。
- [ ] **Step 4: base 路径构建验证**——`npm run build` 后 `web/dist/index.html` 内资源引用以 `/cubebox/` 开头。
- [ ] **Step 5: Commit**

```bash
git add tools/deploy-web.sh web/README.md web/vite.config.ts
git commit -m "feat: gh-pages 部署脚本与 web README"
```

---

### Task 6: 协议文档 + AGENTS.md + 全量回归

**Files:**
- Create: `docs/features/web-config.md`（目录 `docs/features/` 新建）
- Modify: `AGENTS.md`、`web/README.md`（若 Task 5 有遗漏）

**Interfaces:**
- Consumes: 前五个任务全部产出。
- Produces: 文档与干净回归基线。

`docs/features/web-config.md`：协议规格（三命令、`#CFG` JSON 字段表与约束、`#ERR` 语义、部分更新、保活与断开、日志噪声过滤）、浏览器支持（Chromium 系 + HTTPS）、用户操作流程（连接→配置）、部署与 Pages 开启步骤。AGENTS.md 更新四处：项目地图加 `web/`（React 配置器，部署 gh-pages）与 `tools/deploy-web.sh`、`docs/features/`；「远程调试」命令速记加 PING/GETCFG/SETCFG；验证要求加「改 web/ 后 `npm run build` 须通过」。

- [ ] **Step 1: 写文档与 AGENTS.md 更新。**
- [ ] **Step 2: 全量回归**——① ctest 全绿；② `idf.py build` 零新增警告；③ `cd web && npm run build` 通过；④ 真机快速冒烟（PING/GETCFG/SETCFG 各一次 + SNAP）。
- [ ] **Step 3: Commit**

```bash
git add docs/features/web-config.md AGENTS.md
git commit -m "docs: 网页配置器协议规格与 AGENTS.md 项目地图更新"
```

---

## 执行后收尾（不属于单独任务）

- requesting-code-review 派只读 reviewer 审整个分支（含真机验证证据）。
- 用户说 push 时：`git push origin main`；gh-pages 上线需 `tools/deploy-web.sh --push` 且用户在仓库 Settings 开启 Pages。
