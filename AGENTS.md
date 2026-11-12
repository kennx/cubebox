# CubeBox Agent 指南

始终使用中文回复我

本项目是面向 [ESP32-C6-Touch-AMOLED-2.16](./docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md) 设备的 ESP-IDF 嵌入式固件项目。根目录 `AGENTS.md` 应限于热路径规则：项目地图、硬性约束和工作流要求——每项任务都需要知道的内容。

## 工作原则

- 从真实硬件约束、代码事实和验证结果出发思考。如果目标不清晰，先与用户讨论。
- 将代码而非文档视为真理之源。除非用户明确要求，否则不要为了理解实现而阅读普通 Markdown 文档。
- 在修改代码前，先阅读相关代码和最新的约束，并遵循目录树中最近的 `AGENTS.md`。
- 保持修改聚焦。不要顺带进行无关重构。
- **嵌入式优先**：始终考虑内存占用、功耗和硬件约束。ESP32-C6 仅有 512 KB HP SRAM，避免堆碎片化和动态分配。
- **AMOLED 屏约束**：本设备使用 480×480 AMOLED（16.7M 色），对比度极高、响应极快。支持动画和复杂 UI，但需权衡功耗；避免无意义的全屏重绘，利用 `lv_obj_invalidate` 做局部刷新以节省算力。
- **总线独占意识**：LCD（QSPI 4-bit）与 TF 卡（SDSPI 1-bit）共享 CLK/MOSI/MISO。访问 TF 卡前必须上拉 LCD CS（GPIO 15）、拉低 SD CS（GPIO 6），使用完毕后恢复；开机时 `app_main` 已上拉 TF CS 释放总线给 LCD。

## 项目地图

```
main/
├── main.c            # 瘦入口：初始化顺序 + 安装应用 + gui/console 任务
├── config_model.*    # 配置模型：字段验证 + JSON 拼装（纯逻辑，主机可测）
├── config_store.*    # 配置存储与应用：NVS 持久化 + SETCFG 解析 + 副作用 defer 到 gui_task
├── time_sync.*       # 时间同步协调：RTC 开机恢复系统时间 + SNTP 配置 + 同步后写回 RTC
├── low_power.*       # 低功耗状态机（纯逻辑，主机可测）：无操作超时降亮度回时钟，触摸/KEY/WAKE 唤醒；动作由 gui_task 胶水执行
├── hal/              # 硬件抽象层（HAL）
│   ├── hal.h         # 聚合入口，上层统一从这里引入
│   ├── hal_power.*   # AXP2101 电源管理（ALDO3 屏复位）
│   ├── hal_rtc.*     # PCF85063 RTC（电池走时，存 UTC）
│   ├── hal_wifi.*    # Wi-Fi STA 连接（幂等凭据应用）+ 附近 AP 扫描
│   ├── hal_button.*  # KEY 用户按键（GPIO10 内部上拉、低有效，轮询接口）
│   └── hal_display.* # SH8601 QSPI 面板 + LVGL 驱动 + 截图流式输出 + 背光
└── apps/             # 应用层
    ├── apps.{c,h}    # 应用框架：注册表 + 生命周期状态机（主机可测纯 C）
    ├── common/       # 跨应用共享组件（约定见 common/README.md）
    │   ├── dotmatrix/   # 23×23 点阵：纯逻辑网格+字体表（主机可测）+ LVGL widget
    │   └── swipe_nav/   # 左右滑动手势导航（循环切换，走 apps_request_open）
    ├── app_clock/    # 点阵数字时钟（默认第一屏，含 view/ 子目录）
    ├── app_test/     # 点阵「OK」框架验证屏（含 view/ 子目录）
    └── app_template/ # 新应用复制模板（不安装，未被引用代码由链接器 GC 剔除）
tests/                # 主机端单元测试（独立 CMake 工程，ctest）
web/                  # USB 网页配置器（Vite+React+TS，部署 gh-pages；见 web/README.md）
docs/apps/            # 每个应用一份说明文档（强制，见"分层架构约束"）
docs/features/        # 功能规格文档（如 web-config.md：USB 配置协议规格）
```

- `main/main.c`：app_main 初始化顺序（TF CS 释放、I2C 总线、HAL、hal_rtc/hal_wifi/time_sync、config_store_init、安装应用并请求启动默认应用 clock）、gui_task 主循环（截图请求 → `config_store_apply_pending()` → `low_power_service()` → `apps_update()` → `lv_timer_handler`）、USB 调试 console 任务（SNAP/NEXT/REBOOT/WAKE/PING/GETCFG/SETCFG/SCAN/TIME 命令）。应用安装顺序即左右滑动的切换顺序（clock → test，循环）。
- `main/CMakeLists.txt`：主组件构建配置。`hal/` 与 `apps/` 源码按 GLOB_RECURSE 递归收集——**新增/删除源文件后必须 `idf.py reconfigure`**（GLOB 仅在 configure 时求值，直接 build 不会感知新文件）；落在 main/ 根的源文件（如 config_*.c）不被 GLOB 覆盖，须显式加入 SRCS。
- `main/idf_component.yml`：IDF 组件管理器清单。当前依赖 `espressif/esp_lcd_sh8601`、`lvgl/lvgl`、`waveshare/esp_lcd_touch_cst9217` 与 `waveshare/pcf85063a`。
- `docs/hardwares/`：硬件规格与参考文档。
- `docs/firmwares/`：官方固件研究——Waveshare ESP-IDF 示例的目录结构与设备能力清单（蓝牙/BOOT 键官方未实现；触摸方向参数、AXP2101 轨配置等官方事实）。
- `docs/apps/`：应用说明文档，每个 `app_*` 一份（如 `app_clock.md`）。
- `managed_components/`：IDF 组件管理器自动下载的依赖（如 `lvgl__lvgl`、`espressif__esp_lcd_sh8601`）。**对 Agent 只读**。
- `sdkconfig` / `sdkconfig.defaults`：项目 Kconfig 配置。改 `sdkconfig` 后 `idf.py reconfigure` 生效；改 `sdkconfig.defaults` 需删除本地 `sdkconfig`（或 `idf.py fullclean`）重新生成——已有 `sdkconfig` 的条目（含 `# ... is not set`）不会被 defaults 覆盖，`reconfigure` 不吸收。
- `partitions.csv`：Flash 分区表。当前包含 nvs、phy_init、factory（6M app）。

## 分层架构约束

整体对照 [StackChan firmware](https://github.com/m5stack/StackChan/tree/main/firmware)（瘦入口 + `hal/` + `apps/` + `tests/`），层次依赖单向：`apps → hal → 驱动/寄存器`。

### HAL（`main/hal/`，硬件抽象层）

- 一个硬件子系统一个文件：`hal_<subsys>.{c,h}`（电源/显示/触摸，未来音频/IMU/TF…），并统一在 `hal/hal.h` 聚合，上层只 include 聚合头。
- 对上层只暴露统一接口 `hal_<subsys>_<op>()`；初始化期需要的外部句柄（如 i2c 总线）只允许出现在组合根 `main.c` 调用的 `hal_<subsys>_init()` 签名中，头文件用前向声明避免把驱动头传染给上层（见 `hal_power.h`），驱动句柄不得越过组合根进入 apps 层；寄存器细节不出 `hal_*.c`；hal 之间允许互相依赖（如 hal_display 经 hal_power 做屏电源复位）。
- **apps 层绝不直接 include 驱动头或操作 GPIO/寄存器**——一律经 HAL。
- 新增硬件子系统：新建 `hal/hal_<subsys>.{c,h}` → 在 hal.h 聚合 → `idf.py reconfigure`。

### apps（`main/apps/`，应用层）

- 每个应用一个 `app_<name>/` 子目录，独立的应用模块。`app_<name>.{c,h}` 只写生命周期回调；**有 UI 的应用必须建 `view/` 子目录**放界面构建/销毁（`view_<name>.{c,h}`），app 主文件不直接调用 LVGL。
- 生命周期四回调对照 StackChan 的 mooncake AppAbility（契约全文见 `main/apps/apps.h`，模板见 `apps/app_template/`）：
  `on_create`（安装一次，任意上下文，不得碰 LVGL）→ `on_open`（建 UI）→ `on_running`（gui_task 每循环）→ `on_close`（释放资源）。
- **所有 LVGL 调用仅限 gui_task 上下文**（on_open/on_running/on_close 天然满足）；其他任务启动/切换应用只能经 `apps_request_open()` 交接，由 gui_task 的 `apps_update()` 消费；**生命周期回调内切换应用也只许 `apps_request_open`（下一轮生效），禁止嵌套调用 `apps_open`**。
- 新建应用流程：复制 `apps/app_template/` 为 `apps/app_<name>/` → 在 `main.c` `apps_install()` 注册 → **必须同步写 `docs/apps/<name>.md` 说明文档**（功能、生命周期表、文件构成、验证方式，参照 `docs/apps/app_clock.md`）。
- 跨应用共享的 UI 组件/工具放 `apps/common/`（约定见其 README.md）；只有一个使用者时留在 app 内，出现第二个使用者再上移。

### tests（根目录 `tests/`，主机端单元测试）

- 独立于 ESP-IDF 的 CMake 工程：`cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`。
- 只测**可在主机编译的纯逻辑模块**（标准 C，无 IDF/LVGL/硬件依赖，参考 `main/apps/apps.c` 的分层做法）；依赖硬件的行为上机用 USB console 命令 + `tools/` 脚本验证。
- 新增纯逻辑模块时同步加 `test_<module>.c` 与 CMake 条目；修改被测模块后必须跑测试。

## 环境要求

- **ESP-IDF**：`v5.5.1`（项目指定版本；本机实际构建为 v5.5.5 patch 漂移，见 `dependencies.lock`）
- **目标芯片**：`esp32c6`（RISC-V 32-bit 单核，主频高达 160 MHz）
- **无线**：Wi-Fi 6、蓝牙 5、IEEE 802.15.4 (Zigbee 3.0 / Thread)
- **存储**：内置 512 KB HP SRAM、16 KB LP SRAM、320 KB ROM，外挂 16 MB NOR-Flash
- **开发板**：ESP32-C6-Touch-AMOLED-2.16（带锂电池版本，板载 AXP2101 电源管理）

构建前确保已设置目标芯片：
```bash
idf.py set-target esp32c6
```

常用 ESP-IDF 命令速查：
| 命令 | 作用 |
|------|------|
| `idf.py build` | 编译项目 |
| `idf.py flash` | 烧录固件（需已进入下载模式） |
| `idf.py monitor` | 打开串口监视器（115200 baud） |
| `idf.py flash monitor` | 烧录后自动打开监视器 |
| `idf.py menuconfig` | 图形化配置（Kconfig） |
| `idf.py fullclean` | 彻底清理构建产物 |
| `idf.py reconfigure` | 重新生成构建系统 |
| `idf.py erase-flash` | 擦除整片 Flash |
| `idf.py size` | 查看固件内存占用 |
| `idf.py size-files` | 查看各文件内存占用明细 |

> 组合示例：烧录并监视 `idf.py flash monitor`
> 退出监视器：按 `Ctrl+]`

## 通用编码规则

- **编码风格**：遵循 ESP-IDF 官方 C 编码风格（K&R 风格，4 空格缩进）。
  - 文件名：`snake_case.c`
  - 函数名：`snake_case`
  - 类型/结构体：`PascalCase_t`（后缀 `_t`）
  - 局部变量：`snake_case`
  - 全局静态变量：`g_snake_case` 或 `s_snake_case`
  - 宏/枚举：`UPPER_SNAKE_CASE`
- **头文件保护**：优先使用 `#pragma once`。
- **优先使用 ESP-IDF 驱动**：优先使用 ESP-IDF 提供的驱动组件和 BSP 抽象，避免直接操作寄存器，除非性能要求明确。
- **最小化全局状态**：如果不可避免，说明原因并限制在翻译单元内使用 `static`。不要创建仅使用一次的抽象层或组件。
- **内存管理**：
  - DMA 缓冲区必须使用 `heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`，并在失败时断言或处理。
  - 小缓冲区（<1KB）优先栈分配；谨慎使用动态分配，避免在主循环中频繁 `malloc`/`free`。
  - 不要在不可能出现的场景中添加错误处理。
- **I2C 总线**：所有外设挂在总线 0（SDA=GPIO 8, SCL=GPIO 7），注意地址冲突。外设地址一览：
  | 设备 | I2C 地址 | 备注 |
  |------|----------|------|
  | AXP2101 | 0x34 | 电源管理 |
  | ES8311 | 0x18 | 音频编解码 |
  | ES7210 | 0x40 | 回声消除 ADC |
  | QMI8658 | 0x6A | IMU 惯性传感器（SA0 接地） |
  | PCF85063 | 0x51 | RTC 实时时钟（由 AXP2101 电池备份供电） |
  | CST9220 | 0x5A | 电容触摸控制器 |
- **屏幕与触摸**：
  - 板载 2.16 英寸 480×480 高清 AMOLED 屏，驱动 IC 为 CO5300（代码中兼容并常用 `esp_lcd_sh8601` QSPI 驱动）。
  - 电容触控芯片为 CST9220（I2C 0x5A，经 `hal_touch` 用 `waveshare/esp_lcd_touch_cst9217` 组件驱动并注册 LVGL 输入；INT=GPIO 5、RST=GPIO 11、swap_xy+mirror_y；初始化失败不致命）。
  - 屏幕背光通过发送命令 `0x51` 调节亮度，无独立 GPIO 控制。
  - 显示屏复位由 AXP2101 ALDO3 控制电源复位，无独立 RST GPIO。
- **QSPI / SPI 冲突**：LCD（QSPI，4-bit）与 TF 卡（SDSPI，1-bit）共享 CLK/MOSI/MISO GPIO，两者不能同时以最高性能传输，需由软件控制 CS 切换总线访问。典型做法：初始化时上拉 TF CS（GPIO 6）释放总线给 LCD。
- **Strapping Pins**：GPIO 0~4 已用于 LCD QSPI（CS=GPIO 15），GPIO 5 为触摸中断 TP_INT，GPIO 8~9 已用于 I2C / BOOT 按键，GPIO 10 为 KEY 用户按键（低功耗唤醒，见 `hal_button`）。外接设备时注意不要在复位时改变这些引脚电平。
- **LVGL 使用规范**：
  - 当前使用 LVGL v8.x。遵循其对象模型，优先使用 `lv_obj_set_style_xxx` 做样式控制。
  - 避免每帧全屏 `lv_obj_invalidate(lv_scr_act())`，改为仅标记变化区域。
  - **例外**：gui_task 中截图请求触发的一次性 `lv_obj_invalidate(lv_scr_act())` 是 `flush_cb` 截图流架构的必要代价（需强制全屏 flush 让 flush_cb 流出所有条带）。
  - 显示刷新缓冲区大小由 `LCD_WIDTH * 40 * sizeof(lv_color_t)` 定义，修改时需确保 DMA 容量和总线带宽匹配。
- **如果写了 200 行而 50 行就能解决，重写它。**

## 指令更新位置

- 影响几乎所有任务的硬性规则：更新根目录 `AGENTS.md`
- 仅影响特定目录的规则：更新最近的子目录 `AGENTS.md`
- 保持指令更新聚焦，并以代码事实为依据

## 工作流要求

- **git commit 规范**：
  - 标题行使用 Conventional Commits 格式（`feat:` / `fix:` / `refactor:` / `chore:` / `docs:` 等）。
  - body 中按文件或功能分组，说明改了什么、为什么改、影响范围。
  - 修复 bug 需说明根因；架构决策需简要说明理由。
- **文件读取**：如果 **rg** 可用，优先使用 `rg` / `rg --files`。
- **代码修改前**：先阅读相关源码和最新的约束文档；设计变更时，优先遵循现有边界和本地模式。
- **烧录**：日常开发使用 `tools/flash.sh [PORT]` 一键完成编译、烧录。
- **远程调试（USB 控制台命令，`main.c` 的 console 任务经 usb_serial_jtag 驱动直读；主 console 即 USB-Serial/JTAG——sdkconfig.defaults 配置，ESP_LOG 与 SNAP 截图流共用同一通道，screenshot.py 已过滤日志行）**：
  - `SNAP` + `tools/screenshot.py -p PORT -o shot.png`：抓取屏幕 PNG（用系统 python 跑，IDF venv 缺 Pillow）。
  - `NEXT`：循环切换到下一个已安装应用（触摸失效时的兜底验证途径）。
  - `WAKE`：注入一次唤醒活动，退出低功耗态（无法远程触摸/按 KEY 时的验证途径）。
  - `REBOOT`：远程重启（抓完整启动日志用）。
  - `SCAN`：阻塞扫描附近未隐藏 AP（~2-4s），回 `#NETWORKS [{ssid,rssi,secure}]`（网页 Wi-Fi 卡片用）。
  - `TIME`：回 `#TIME {synced,utc,local}`（设备时间与 NTP 同步状态，网页时钟页用）。
  - `PING` / `GETCFG` / `SETCFG {json}`：网页配置器协议（握手 / 读配置 / 部分更新，响应 `#HELLO`/`#CFG`/`#ERR`，配置持久化 NVS，亮度/时区即时生效，Wi-Fi/NTP 幂等应用）。完整规格见 `docs/features/web-config.md`；网页在 `web/`（`tools/deploy-web.sh` 部署 gh-pages）。
- **验证要求**：
  - 完成功能后，必须执行构建验证：`idf.py build`
  - 若进行了配置变更或依赖变更，执行完整清理构建：`idf.py fullclean && idf.py build`
  - 修改 `main/apps/apps.c` 等主机可测模块后，运行 `tests/` 测试（命令见 tests/README.md）
  - 修改 `web/` 后，`cd web && npm run build` 须通过（tsc 严格模式）
  - 在声称完成前，确认编译无错误、无新增警告
