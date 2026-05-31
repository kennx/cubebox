# Repository-level Agent Guide

始终使用中文回复我

这是一个基于 ESP-IDF 的嵌入式固件项目，目标设备为 [ESP32-C6-Touch-AMOLED-2](./docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md).16。保持根 `AGENTS.md` 聚焦于高频规则：项目结构、硬约束和开发流程。

## Working Principles

- 从真实需求、代码事实和验证结果出发思考。如果目标不清晰，先与用户讨论。
- 代码是唯一的真相来源。除非用户明确要求，不要仅通过阅读 Markdown 来理解实现。
- 修改代码前，先阅读相关代码和最近的约束，并遵循目录树中最近的 `AGENTS.md`。
- 保持修改聚焦。不要顺带做无关重构。

## Project Map

- `docs/features/`：功能需求与说明文档
- `docs/hardwares/`：硬件规格与参考文档
- `docs/issues/`：已知问题与排障记录
- `docs/superpowers/`：设计规格与实现计划
  - `plans/`：实现计划
  - `specs/`：设计规格

> 源码目录将在固件开发过程中建立。典型 ESP-IDF 项目结构为：
> - `main/`：主应用程序代码
> - `components/`：自定义组件
> - `sdkconfig`：项目配置

## Environment Requirements

- **ESP-IDF**：`v6.0.1`（项目指定版本）
- **目标芯片规格**：
  - **主控**：`esp32c6`（RISC-V 32-bit 单核，主频高达 160 MHz）
  - **无线**：Wi-Fi 6、蓝牙 5、IEEE 802.15.4 (Zigbee 3.0 / Thread)
  - **存储**：内置 512 KB HP SRAM、16 KB LP SRAM、320 KB ROM，外挂 16 MB NOR-Flash
- **开发板硬件**：
  - **型号**：ESP32-C6-Touch-AMOLED-2.16（带锂电池版本，板载 AXP2101 电源管理，3.7V MX1.25 电池接口）
  - **接口与扩展**：Type-C，引出 1路 I2C、1路 UART、1路 USB 焊盘
  - **交互按钮**：板载 KEY、BOOT 两个侧边自定义按键

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

## General Coding Rules

- 遵循 ESP-IDF 官方 C/C++ 编码风格（K&R 风格，4 空格缩进）。
- 优先使用 ESP-IDF 提供的驱动组件和 BSP 抽象，避免直接操作寄存器，除非性能要求明确。
- **I2C 总线**：所有外设挂在总线 0（SDA=GPIO 8, SCL=GPIO 7），注意地址冲突。外设地址一览：
  | 设备 | I2C 地址 | 备注 |
  |------|----------|------|
  | AXP2101 | 0x34 | 电源管理 |
  | ES8311 | 0x18 | 音频编解码 |
  | ES7210 | 0x40 | 回声消除 ADC |
  | QMI8658 | 0x6A | IMU 惯性传感器（SA0 接地） |
  | PCF85063 | 0x51 | RTC 实时时钟（由 AXP2101 电池备份供电） |
  | CST9220 | 0x5A | 电容触摸控制器（代码中驱动使用 CST9217 库） |
- **屏幕与触摸**：板载 2.16 英寸 480x480 高清 AMOLED 屏，驱动 IC 为 CO5300（代码中兼容并常用 `esp_lcd_sh8601` QSPI 驱动）；电容触控芯片为 CST9220（代码驱动使用 `esp_lcd_touch_cst9217`，I2C 通信）。
- **QSPI / SPI 冲突**：LCD（QSPI，4-bit）与 TF 卡（SDSPI，1-bit）共享 CLK/MOSI/MISO GPIO，两者不能同时以最高性能传输，需由软件控制 CS 切换总线访问。
- **屏幕背光**：通过发送命令 `0x51` 调节亮度，无独立 GPIO 控制。
- **显示屏复位**：由 AXP2101 ALDO3 控制电源复位，无独立 RST GPIO。
- **Strapping Pins**：GPIO 0~5 已用于 LCD QSPI，GPIO 8~9 已用于 I2C / BOOT 按键。外接设备时注意不要在复位时改变这些引脚电平。
- 不要为仅使用一次的代码创建抽象层或组件。
- 不要在不可能出现的场景中添加错误处理。
- 如果写了 200 行而 50 行就能解决，重写它。

## Where to Update Instructions

- 影响几乎所有任务的硬性规则：更新根目录 `AGENTS.md`
- 仅影响特定目录的规则：更新该子目录下最近的 `AGENTS.md`
- 保持指令更新聚焦，并以代码事实为支撑

## Workflow Requirements

- 读取文件，如果可用 `rg` 最好使用 `rg` / `rg --files`。
- 修改代码前先阅读相关源码和最近的约束文档
- 设计变更时，优先遵循现有边界和本地模式
- 提交 PR 时，标题必须遵循 Conventional Commit 风格，例如 `feat: add clock display component`
- PR 描述中必须关联相关 issue 或解释问题，然后描述变更内容；不要留下占位符文本
- 不要提交模糊的 AI 生成 PR 文本
- 完成功能后，确保固件能正常编译：
  ```bash
  idf.py build
  ```
- 烧录固件前，确认已进入下载模式（按住 BOOT 按键同时上电或按 RESET）
