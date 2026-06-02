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
- **总线独占意识**：LCD（QSPI 4-bit）与 TF 卡（SDSPI 1-bit）共享 CLK/MOSI/MISO。访问 TF 卡前必须上拉 LCD CS（GPIO 15）、拉低 SD CS（GPIO 6），使用完毕后恢复。

## 项目地图

- `main/main.c`：当前唯一的应用程序源文件。包含 PMIC（AXP2101）、RTC（PCF85063）、LCD（SH8601 QSPI）初始化、LVGL 显示驱动与主 UI 任务。
- `main/CMakeLists.txt`：主组件构建配置。新增源文件或依赖时必须修改。
- `main/idf_component.yml`：IDF 组件管理器清单。当前依赖 `espressif/esp_lcd_sh8601` 与 `lvgl/lvgl`。
- `docs/features/`：功能需求与说明文档
- `docs/hardwares/`：硬件规格与参考文档
- `docs/issues/`：已知问题与排障记录
- `docs/superpowers/`：设计规格与实现计划
  - `plans/`：实现计划（如矩阵时钟重构计划）
  - `specs/`：设计规格（如 Nothing Phone 风格点阵时钟设计）
- `managed_components/`：IDF 组件管理器自动下载的依赖（如 `lvgl__lvgl`、`espressif__esp_lcd_sh8601`）。**对 Agent 只读**。
- `sdkconfig` / `sdkconfig.defaults`：项目 Kconfig 配置。修改后需重新运行 `idf.py reconfigure`。
- `partitions.csv`：Flash 分区表。当前包含 nvs、phy_init、factory（6M app）、storage（3M SPIFFS）。

> 随着功能增长，代码应从 `main.c` 逐步拆分为 `components/` 或 `main/` 下的模块化文件（如 `bsp_power.c`、`bsp_lcd.c`、`ui_clock.c`）。拆分原则：一个文件聚焦一个子系统，避免 `main.c` 超过 800 行。

## 环境要求

- **ESP-IDF**：`v5.5.1`（项目指定版本）
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
  | CST9220 | 0x5A | 电容触摸控制器（代码中驱动使用 CST9217 库） |
- **屏幕与触摸**：
  - 板载 2.16 英寸 480×480 高清 AMOLED 屏，驱动 IC 为 CO5300（代码中兼容并常用 `esp_lcd_sh8601` QSPI 驱动）。
  - 电容触控芯片为 CST9220（代码驱动使用 `esp_lcd_touch_cst9217`，I2C 通信）。
  - 屏幕背光通过发送命令 `0x51` 调节亮度，无独立 GPIO 控制。
  - 显示屏复位由 AXP2101 ALDO3 控制电源复位，无独立 RST GPIO。
- **QSPI / SPI 冲突**：LCD（QSPI，4-bit）与 TF 卡（SDSPI，1-bit）共享 CLK/MOSI/MISO GPIO，两者不能同时以最高性能传输，需由软件控制 CS 切换总线访问。典型做法：初始化时上拉 TF CS（GPIO 6）释放总线给 LCD。
- **Strapping Pins**：GPIO 0~5 已用于 LCD QSPI，GPIO 8~9 已用于 I2C / BOOT 按键。外接设备时注意不要在复位时改变这些引脚电平。
- **LVGL 使用规范**：
  - 当前使用 LVGL v8.x。遵循其对象模型，优先使用 `lv_obj_set_style_xxx` 做样式控制。
  - 避免每帧全屏 `lv_obj_invalidate(lv_scr_act())`，改为仅标记变化区域。
  - **例外**：`gui_task` 中的 `lv_obj_invalidate(lv_scr_act())` 是 `lvgl_flush_cb` 像素拦截架构的必要代价。LVGL 不知道手写点阵时钟和状态栏的像素覆盖，因此必须强制全屏 flush 以确保自定义像素被重新计算。这是已知功耗权衡，已通过每秒 1 次刷新将频率降到最低。
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
- **代码修改前**：先阅读相关源码和最近的约束文档；设计变更时，优先遵循现有边界和本地模式。
- **验证要求**：
  - 完成功能后，必须执行构建验证：`idf.py build`
  - 若进行了配置变更或依赖变更，执行完整清理构建：`idf.py fullclean && idf.py build`
  - 在声称完成前，确认编译无错误、无新增警告
