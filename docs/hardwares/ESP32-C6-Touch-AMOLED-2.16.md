# ESP32-C6-Touch-AMOLED-2.16 硬件参考

> 来源：Waveshare 官方文档 + 开源示例代码  
> 适用 AI Agent：开发 ESP-IDF / Arduino 固件前必读

---

## 1. 基本信息

> **本设备为带锂电池版本**，型号 `ESP32-C6-Touch-AMOLED-2.16`，板载 MX1.25 锂电池接口与 AXP2101 充放电管理。另有 `-EN` 后缀的无电池版本，硬件规格基本一致。

| 项目 | 参数 |
|------|------|
| 型号 | ESP32-C6-Touch-AMOLED-2.16（带锂电池版本） |
| 主控 | ESP32-C6（RISC-V 32-bit 单核，主频高达 160 MHz，出色的射频性能） |
| Flash | 16 MB（外接 NOR-Flash） |
| SRAM | 512 KB HP SRAM + 16 KB LP SRAM + 320 KB ROM |
| 无线 | Wi-Fi 6、Bluetooth 5 LE、IEEE 802.15.4（Zigbee 3.0 / Thread）无线通信 |
| 天线 | 板载贴片天线，支持拆焊电阻切换为 IPEX 1 代外接天线 |
| 供电 | 采用 Type-C 接口（高兼容性与便捷性） / 3.7V MX1.25 锂电池接口（带充放电管理） |
| 开发框架 | ESP-IDF（推荐 v5.5.2+）、Arduino IDE（arduino-esp32 v3.3.0+） |

### 1.1 ESP32-C6 芯片级规格指标

根据乐鑫官方数据手册（`esp32-c6_datasheet_cn.pdf`），作为本开发板的主脑，ESP32-C6 芯片的核心技术规格如下：

#### CPU 与存储系统
- **高性能处理器 (HP CPU)**：
  - 32-bit RISC-V 单核处理器。
  - 工作频率高达 160 MHz。
- **低功耗处理器 (LP CPU)**：
  - 32-bit RISC-V 单核处理器，工作频率最高可达 20 MHz，用于支持极低功耗状态下的任务调度。
- **内置存储容量**：
  - **320 KB ROM**：芯片上电引导及固件底层库。
  - **512 KB HP SRAM**：运行时的代码和主运行内存。
  - **16 KB LP SRAM**：用于超低功耗模式（Deep-sleep）下的状态数据与 LP 内核保持。
  - **8 KB LP ROM**：支持低功耗模式启动。
- **外挂存储支持**：
  - 支持外接 Flash 和 PSRAM 存储扩展（本板已搭载 16 MB SPI 接口的 NOR-Flash）。

#### 无线射频与多协议通信
- **Wi-Fi 6**：
  - 2.4 GHz 频段，支持 IEEE 802.11 b/g/n 与 802.11ax（Wi-Fi 6）协议。
  - 支持 20 MHz 带宽物理层，支持更省电的 TWT（目标唤醒时间）网络控制。
- **蓝牙低功耗 (Bluetooth LE)**：
  - 兼容 Bluetooth 5 标准，支持高功率广播模式（最高 +20 dBm 发射功率）。
  - 支持 Bluetooth Mesh 组网。
  - 支持 125 Kbps、500 Kbps、1 Mbps 及 2 Mbps 的传输速率。
- **802.15.4 射频**：
  - 全面支持 Thread 1.3 和 Zigbee 3.0 通信协议，天然支持接入 Matter 智能家居生态。

#### 硬件安全机制
- **数据加密加速器**：提供 AES-128/256、Hash、RSA、HMAC 以及数字签名 (DS) 的硬件级加速计算。
- **安全启动 (Secure Boot v2)**：保障固件可信签名，杜绝盗刷恶意固件。
- **闪存加密 (Flash Encryption)**：实时对外部 NOR-Flash 数据进行硬件加解密。
- **随机数发生器 (RNG)**：真随机数产生器（RNG），提升密钥分发与通信安全性。
- **1024-bit OTP 存储区**：最高 256 位可供用户编写设备身份秘钥或生产出厂特征码。

#### 电源管理与能耗策略
- 搭载**智能电源管理单元 (PMU)**，实现多模式低能耗运作。
- 芯片功耗模式细分：
  - **Active 模式**：CPU 内核与射频部分均运行，最大功耗。
  - **Modem-sleep 模式**：无线射频暂时停用以降低待机功耗，CPU 依然全速运转。
  - **Light-sleep 模式**：CPU 和大部分外设、无线均挂起，可用定时唤醒或 GPIO 极速恢复运行。
  - **Deep-sleep 模式**：仅 LP CPU 和 LP 存储保持极低状态维持，整机功耗为微安级别，通过 RTC 外设或引脚信号随时唤醒。

---

## 2. 板载外设一览

| 外设 | 芯片型号 | 接口 | 用途 |
|------|----------|------|------|
| AMOLED 显示屏 | CO5300（兼容 SH8601 驱动） | QSPI | 2.16 英寸，480×480，16.7M 色 |
| 电容触控 | CST9220（驱动库使用 CST9217） | I2C | 多点电容触摸 |
| 电源管理 | AXP2101 | I2C | 充放电、DC-DC、LDO、电源开关 |
| 六轴 IMU | QMI8658 | I2C | 3 轴加速度 + 3 轴陀螺仪 |
| RTC 时钟 | PCF85063 | I2C | 实时时钟，AXP2101 电池备份供电 |
| 音频编解码 | ES8311 | I2C + I2S | DAC 音频输出 |
| 回声消除 ADC | ES7210 | I2C + I2S | 麦克风 ADC + 回声消除 |
| 扬声器功放 | 板载功放芯片 | 模拟 | 外接 2PIN 扬声器焊点 |
| 麦克风 | 双麦克风阵列 | 模拟 | 连接 ES7210 |
| TF 卡槽 | — | SPI（SDSPI）| FAT32 数据扩展 |
| 按键 | BOOT、PWR、KEY | GPIO/PMU | 自定义功能 |

---

## 3. GPIO 引脚分配

### 3.1 显示屏（QSPI）

| 功能 | GPIO | 备注 |
|------|------|------|
| LCD_CS | 15 | QSPI 片选 |
| LCD_PCLK | 0 | QSPI 时钟 |
| LCD_D0 | 1 | QSPI 数据线 0 |
| LCD_D1 | 2 | QSPI 数据线 1 |
| LCD_D2 | 3 | QSPI 数据线 2 |
| LCD_D3 | 4 | QSPI 数据线 3 |
| LCD_RST | NC | 由 AXP2101 ALDO3 控制电源复位 |
| 背光 | — | 通过发送命令 0x51 调节亮度，无独立 GPIO |

> **注意**：官方文档标注驱动芯片为 CO5300（Chipone），但 Waveshare 官方 ESP-IDF 示例中实际使用 `esp_lcd_sh8601` 组件驱动，初始化命令序列参考 `display_bsp.cpp` 中的 `lcd_init_cmds`。CO5300 与 SH8601 寄存器兼容。

### 3.2 触摸（I2C）

| 功能 | GPIO | 备注 |
|------|------|------|
| TP_SDA | 8 | 与系统 I2C 总线共享 |
| TP_SCL | 7 | 与系统 I2C 总线共享 |
| TP_RST | 11 | 触摸复位 |
| TP_INT | 5 | 触摸中断 |

> 触控芯片官方标称为 CST9220，但驱动库使用 `esp_lcd_touch_cst9217`，I2C 地址 **0x5A**。

### 3.3 I2C 总线 0（SDA=GPIO 8, SCL=GPIO 7）

所有 I2C 设备挂在同一总线上：

| 设备 | I2C 地址 | 备注 |
|------|----------|------|
| AXP2101 | 0x34 | 电源管理 |
| ES8311 | 0x18 | 音频编解码 |
| ES7210 | 0x40 | 回声消除 ADC |
| QMI8658 | 0x6A | IMU（SA0 接地，默认地址） |
| PCF85063 | 0x51 | RTC 实时时钟 |
| CST9217 | 0x5A | 电容触摸控制器 |

### 3.4 音频 I2S

| 功能 | GPIO | 备注 |
|------|------|------|
| I2S_MCLK | 19 | 主时钟 |
| I2S_BCLK | 20 | 位时钟 |
| I2S_WS | 22 | 左右声道时钟 |
| I2S_DIN | 21 | 麦克风数据输入 |
| I2S_DOUT | 23 | 扬声器数据输出 |

### 3.5 TF 卡（SDSPI）

TF 卡与 LCD QSPI 共享 CLK、MOSI、MISO 数据线，使用独立片选：

| 功能 | GPIO | 备注 |
|------|------|------|
| SD_CLK | 0 | 与 LCD_PCLK 共享 |
| SD_MOSI | 1 | 与 LCD_D0 共享 |
| SD_MISO | 2 | 与 LCD_D1 共享 |
| SD_CS | 6 | 独立片选 |

> **注意**：LCD 使用 QSPI 模式（4-bit），SD 卡使用标准 SPI 模式（1-bit）。两者不能同时以最高性能传输，需由软件控制 CS 切换总线访问。

### 3.6 按键

| 按键 | GPIO/连接 | 备注 |
|------|-----------|------|
| BOOT | GPIO 9 | 侧边自定义按键。按住 + 重新上电进入下载模式；也可作为普通按键使用 |
| PWR | AXP2101 PWRON | 电源按键。长按 4 秒关机，短按开机；不由 ESP32 GPIO 直接读取 |
| KEY | GPIO 10 | 侧边自定义按键（原理图 Key3）。方便进行二次功能开发；固件经 `hal_button` 驱动（低功耗唤醒）。官方源码未使用该引脚，**来源：官方原理图 PDF 解析**（Key1=GPIO9/CHIP_PU、Key2=PWRON、Key3=GPIO10） |

> KEY（GPIO 10）无外部上拉电阻，按下接地：驱动需使能内部上拉、低电平有效。GPIO 10 现无其他外设占用。

### 3.7 外接焊盘与引出接口

为了方便外接设备与调试，开发板在背面或边缘引出了以下焊盘接口：
- **1路 I2C 接口**
- **1路 UART 接口**
- **1路 USB 接口焊盘**

（具体 GPIO 映射和焊盘排布请参考硬件原理图）

---

## 4. 显示屏详细参数

| 参数 | 值 |
|------|-----|
| 面板类型 | AMOLED |
| 尺寸 | 2.16 英寸 |
| 分辨率 | 480 × 480 |
| 颜色深度 | 16.7M（24-bit） |
| 亮度 | 600 cd/㎡ |
| 对比度 | 100000:1 |
| 通信接口 | QSPI |
| 驱动 IC | CO5300（兼容 SH8601） |
| 触控 IC | CST9220（驱动使用 CST9217） |
| 触控接口 | I2C |

> **AMOLED 屏幕性能优势**：
> - **视觉效果优秀**：16.7M 色色彩深度，对比度高达 100000:1，能够清晰、逼真地显示彩色图片。
> - **视角与响应**：具备极广的可视角度和更快速的响应时间。
> - **结构与能耗**：屏幕机身更为纤薄，同时兼具低功耗和结构灵活性等优点。

---

## 5. 电源管理（AXP2101）

AXP2101 通过 I2C（地址 0x34）控制整机电源。

> **AXP2101 电源芯片优势**：
> - **高效电源管理**：高集成的电源系统，优化系统各部件的能耗。
> - **灵活的多路输出**：支持多路 DC-DC 与 LDO 输出，可提供不同大小的输出电压，全面覆盖外设供电需求。
> - **充电与寿命管理**：板载 3.7V MX1.25 锂电池接口，提供高效的充放电管理，并针对延长电池寿命进行了深度优化。

各路输出电压分配一览：

| 输出 | 电压 | 负载 |
|------|------|------|
| DC1 | 3.3V | 主系统供电 |
| ALDO1 | 3.3V | 麦克风（MIC） |
| ALDO2 | 3.3V | 可由软件开关 |
| ALDO3 | 3.3V | 显示屏电源/复位控制 |
| ALDO4 | 3.3V | — |

充电参数（参考值）：
- 充电截止电压：4.1V
- 预充电电流：50 mA
- 恒流充电电流：400 mA（可配置 200/300/400 mA）
- 终止充电电流：25 mA

---

## 6. 六轴 IMU（QMI8658）

| 参数 | 值 |
|------|-----|
| 接口 | I2C（地址 0x6A） |
| 加速度计量程 | ±2/4/8/16 g |
| 陀螺仪量程 | ±16/32/64/128/256/512/1024/2048 °/s |
| FIFO | 1536 字节 |
| 内置功能 | 计步器、敲击检测、运动检测、静止检测 |

> **IMU 核心作用**：
> - 内置 3 轴加速度计与 3 轴陀螺仪，能够实时精准地检测设备的运动姿态。
> - 支持计步器、翻转检测、敲击检测等多姿态动作识别功能开发。

---

## 7. RTC 实时时钟（PCF85063）

板载 PCF85063 实时时钟（RTC）芯片：
- **I2C 通信地址**：`0x51`
- **不间断供电**：RTC 芯片通过 AXP2101 接入电池，确保在外部 USB 主电源断电时，仍能由锂电池持续供电以保持时钟走时。
- **开发应用**：极低功耗，可用于日历、硬件定时唤醒和独立高精度时间戳记录。

---

## 8. 注意事项

### 7.1 Strapping Pins
ESP32-C6 的 strapping pins 包含 GPIO 0/1/2/3/4/5/8/9/15 等。本开发板中：
- GPIO 0~5 已用于 LCD QSPI
- GPIO 8~9 已用于 I2C / BOOT 按键

Waveshare 在设计中已处理这些引脚的启动配置，正常开发无需额外关注。但如果要外接强上拉/下拉设备，需注意不要在复位时改变这些引脚电平。

### 7.2 进入下载模式
按住 **BOOT** 按键，同时给开发板上电或按 RESET，即可进入固件下载模式。

### 7.3 代码中的引脚冲突
Waveshare 官方 ESP-IDF 示例中的 `user_config.h` 文件里 `BSP_LCD_CS` 与 `BSP_LCD_TOUCH_INT` 的 GPIO 编号存在笔误（将 5 和 15 写反了），但 `display_bsp.cpp` 实际使用的是 `DisplayPort` 类构造函数的默认参数（`cs=15, tp_int=5`），因此示例代码仍可正常运行。若自行移植，请以 **本文档表格** 或 `DisplayPort` 默认参数为准。

### 7.4 目标芯片设置
使用 ESP-IDF 时：
```bash
idf.py set-target esp32c6
```

---

## 9. 参考资源

| 资源 | 链接 |
|------|------|
| Waveshare 官方文档 | https://docs.waveshare.net/ESP32-C6-Touch-AMOLED-2.16 |
| Waveshare GitHub 示例 | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16 |
| ESP32-C6 技术参考手册 | https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6 |
| CO5300 Datasheet | https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf |
| QMI8658A Datasheet | https://files.waveshare.com/upload/5/5f/QMI8658A_Datasheet_Rev_A.pdf |
| AXP2101 Datasheet | 见 Waveshare 文档站 Resources 页面 |
| ES8311 Datasheet | 见 Waveshare 文档站 Resources 页面 |
| PCF85063 Datasheet | 见 Waveshare 文档站 Resources 页面 |
