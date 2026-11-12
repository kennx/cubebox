# Waveshare 官方固件设备能力清单(ESP32-C6-Touch-AMOLED-2.16)

> 依据官方仓库 `02_Example/ESP-IDF-v5.5.3/` 九个示例的**源码实现**整理(2026-11 逐文件阅读,来源 commit 见仓库 v1.0.0, 2026-03-30)。
> 目录结构与逐文件作用见 [official-examples-structure.md](./official-examples-structure.md);本板硬件规格见 [../hardwares/ESP32-C6-Touch-AMOLED-2.16.md](../hardwares/ESP32-C6-Touch-AMOLED-2.16.md)。

## 能力总表

| 能力 | 芯片/实现 | 总线与引脚 | 示例项目 | 状态 |
|------|-----------|------------|----------|------|
| 电源管理/电池 | AXP2101(XPowersLib) | I2C0 @0x34, 100kHz | 01 | ✅ 已实现 |
| 显示 | SH8601/CO5300 AMOLED 480×480 | QSPI 4-bit, 40MHz | 07/08/09 | ✅ 已实现 |
| 背光调节 | SH8601 命令 0x51 | — (无独立 GPIO) | 07/08/09 | ✅ 已实现 |
| 触摸 | CST9220(cst9217 库) | I2C0 @0x5A, 400kHz;INT=5/RST=11 | 07/08/09 | ✅ 已实现 |
| IMU 六轴 | QMI8658(waveshare/qmi8658) | I2C0 @0x6B(SA0 接高电平) | 02 | ✅ 已实现 |
| RTC | PCF85063(waveshare/pcf85063a) | I2C0 @0x51 | 03 | ✅ 已实现 |
| TF 卡 | SDSPI 1-bit + FAT | SPI2:CLK=0/MOSI=1/MISO=2/CS=6 | 04 | ✅ 已实现 |
| 音频输出 | ES8311 + esp_codec_dev | I2C0 @0x18;I2S TDM | 07 | ✅ 已实现 |
| 音频输入 | ES7210 + esp_codec_dev | I2C0 @0x40;I2S TDM | 07 | ✅ 已实现 |
| WiFi STA | esp_wifi(Wi-Fi 6) | 内置射频 | 05 | ✅ 已实现 |
| WiFi SoftAP | esp_wifi | 内置射频 | 06 | ✅ 已实现 |
| GUI 框架 | LVGL v8.4 / v9.5 + esp_lvgl_adapter | — | 07/08/09 | ✅ 已实现 |
| 文件存储 | SPIFFS(音频 PCM) | flash storage 分区 3M | 07 | ✅ 已实现 |
| **蓝牙** | ESP32-C6 内置 BT5 | — | — | ❌ **九个示例均未实现**(全仓 grep 无 esp_bt/bluetooth;XiaoZhi-v2.2.5 固件亦未启用) |
| **BOOT 按键** | GPIO 9(硬件存在) | — | — | ❌ **源码未使用**(无任何按键驱动/gpio 9 引用;仅 PMIC 库内部 PWR 按键常量,与本板无关) |
| **Zigbee/Thread** | ESP32-C6 内置 802.15.4 | — | — | ❌ 未实现 |

## 各能力实现详情

### 1. 电源管理(AXP2101,`components/pmicpower/power_bsp.cpp`)

- 库:[lewisxhe/XPowers](https://github.com/lewisxhe/XPowers)(vendored),`XPowersPMU` 对象 + 回调式读写(每次 I2C 失败重试 3 次,间隔 100ms)。
- 轨配置(`Custom_PmicRegisterInit`):**DC1、ALDO1、ALDO2、ALDO3、ALDO4 全部 3.3V**;均带"读回校验,不同才写"逻辑。
- 充电配置:VBUS 电流限流 **2A**、预充电流 50mA、恒流充电 **500mA**、充电终止电流 50mA。
- 运行时 API:
  - `Axp2101_isChargingTask()`:2s 轮询,打印充电状态机(涓流/预充/恒流/恒压/完成/停止)与电池电压 mV。
  - `Axp2101_SetAldo3(on/off)`:ALDO3 开关 —— **屏幕电源复位引脚**(display_bsp 用它做高-低-高复位,本板屏无独立 RST GPIO)。
  - `Axp2101_SetAldo2(on/off)`:ALDO2 开关(示例中未被调用,官方保留接口)。

### 2. 显示(SH8601 QSPI AMOLED,`port_bsp/display_bsp.cpp`)

- 初始化:SPIO Quad 模式,PCLK **40MHz**,`lcd_cmd_bits=32`/`param_bits=8`,`trans_queue_depth=1`,RGB565 大端;15 条 vendor init 命令(SLPOUT→PAGESEL→…→MADCTL 0x30→CASET/RASET 480→DISPON)。
- 复位:`Axp2101_SetAldo3` 高(100ms)→低(100ms)→高(100ms)。
- 背光 `Set_Backlight(0-100)`:命令 0x51,线性映射 0-255(与 cubobox 相同的 `(0x02<<24)|(0x51<<8)` 命令格式)。
- 旋转 `Set_Rotate(1/0)`:命令 0x36 写 0x30/0x00(仅两档)。
- 亮度演示(08/09):后台任务每 2s 步进 -20%,到 0 回 100,验证背光全范围。

### 3. 触摸(CST9220,`display_bsp.cpp` + `waveshare/esp_lcd_touch_cst9217 ^1.0.4`)

- I2C 地址 0x5A(cst9217 库默认配置宏),总线 **400kHz**,挂共享 I2C0。
- 引脚:**INT=GPIO5,RST=GPIO11**(reset 低有效、int 低有效)。
- 方向参数:`x_max=y_max=480`,`swap_xy=1, mirror_x=0, mirror_y=1` —— 官方标定的触摸→屏幕坐标映射。
- ⚠️ `user_config.h` 中 `BSP_LCD_CS=5 / BSP_LCD_TOUCH_INT=15` 与实际相反,且各 main 实例化 `DisplayPort` 用构造函数默认值(`cs=15, tp_int=5, tp_reset=11`)——**以 display_bsp.cpp 默认值为准**(与 cubobox 实测一致:LCD CS=15 工作正常)。

### 4. IMU 六轴(QMI8658,`waveshare/qmi8658 ^1.0.1` 组件)

- 地址 0x6B(`QMI8658_ADDRESS_HIGH`,SA0 接高电平),挂共享 I2C0（CubeBox 实机验证）。
- 配置(02 示例):加速度 **±8g @1000Hz**、陀螺仪 **512dps @1000Hz**、单位 m/s² 与 rad/s、`qmi8658_set_display_precision(4)` 设 4 位换算精度(注意:main 的日志 snprintf 用 `%.2f`,实际打印 2 位小数)。
- 读取流程:500ms 轮询 `qmi8658_is_data_ready`(DRDY)→ `qmi8658_read_sensor_data` 得六轴浮点 → 日志打印。

### 5. RTC(PCF85063,`waveshare/pcf85063a ^1.1.1` 组件)

- 地址 0x51(`PCF85063A_ADDRESS`),挂共享 I2C0;由 AXP2101 电池备份供电(掉电走时)。
- API:`pcf85063a_set_time_date`(示例开机设 2026-01-01 08:00:00)、`pcf85063a_get_time_date` + `pcf85063a_datetime_to_str`(每秒读打印)。
- 时间结构含年(完整 4 位)/月/日/时/分/秒(组件还提供星期,示例未展示)。

### 6. TF 卡(SDSPI,`port_bsp/sdcard_bsp.cpp`)

- 接线:SDSPI 1-bit,CLK=GPIO0 / MOSI=GPIO1 / MISO=GPIO2 / CS=GPIO6,`SPI2_HOST`(与 LCD QSPI 共享 CLK/MOSI/MISO——**官方示例中 04(卡)与 07/08/09(屏)不同时运行**,被注释的旧构造函数保留了"复用 Display SPI host"的共享方案)。
- 挂载:`esp_vfs_fat_sdspi_mount("/sdcard", …)`,不自动格式化,max_files=5,簇 512B;成功后 `sdmmc_card_print_info` 打印卡信息。
- 文件 API:`SDPort_WriteFile/ReadFile`(整文件)、`SDPort_ReadOffset/WriteOffset`(偏移读/写,append 可选)、写前 `sdmmc_get_status` 卡在位校验。

### 7. 音频(ES8311 + ES7210,`port_bsp/codec_bsp.cpp` + `ExternLib/codec_board`)

- 链路:esp_codec_dev 框架,I2S **TDM** 模式(Philips,4 slot,32bit,默认 16kHz 起步);引脚来自 `board_cfg.txt` 的 `C6_AMOLED_2_16` 条目:**MCLK=19、BCLK=20、WS=22、DIN=21(麦克风入)、DOUT=23(扬声器出)**。
- 播放:ES8311 @I2C 0x18(400kHz),无独立 PA GPIO(`pa:-1`),`CodecPort_SetSpeakerVol(0-100)`;录音:ES7210 @0x40,`CodecPort_SetMicGain(dB)`(示例 35.0)。
- 07 示例流程:16kHz/2ch/16bit(64KB/s)→ "Recording" 录 **3 秒(192000 字节上限)**到 SPIFFS `/spiffs/rec.raw` → "Play Recording" 回放;"Play Music" 播放 SPIFFS 内 `mood.pcm`(构建时经 `spiffs_create_partition_image` 烧入 storage 分区)。
- CodecPort 内置任务:`MusicTask`(循环播放固件内嵌 canon.pcm,24kHz/2ch/16bit,音量 80)与 `EchoTask`(ES7210 读 1024B → ES8311 即写,回声对讲);07 的 main 未启用这两个任务(自带事件组逻辑)。
- 调试 API:`Codec_Set/GetCodecReg` 按名字("es8311"/"es7210")直读写编解码寄存器。
- ⚠️ 代码观察:`EchoTask` 里 `heap_caps_malloc(1024, MALLOC_CAP_SPIRAM)` 且未判空——C6 无 PSRAM,该函数返回 NULL,任务若被启用会空指针读写崩溃(07 未调用它,属死代码风险)。

### 8. WiFi STA(05)/ SoftAP(06)

- 标准流程:NVS init(失败即擦除重试)→ `esp_netif_init` → 默认事件循环 → **同时创建** STA 与 AP 两个默认 netif → `esp_wifi_init` → 事件回调注册 → 启动。
- STA(05):硬编码 SSID `ESP32_STA` / 密码 `12345678`;`WIFI_EVENT_STA_START` 自动连接,`IP_EVENT_STA_GOT_IP` 打印 IP,断开清空。
- AP(06):SSID `ESP32_AP` / 密码 `12345678`,WPA_WPA2_PSK,**最多 4 个客户端**;打印接入/断开客户端 MAC。
- (06 源码 STA 分支残留开发者自家 SSID `K2P`,不可用,以 05 为准。)

### 9. LVGL v8/v9 移植(`app_bsp/lvgl_bsp.cpp` + `espressif/esp_lvgl_adapter ^0.3.0`)

- 同一份 `lvgl_bsp.cpp` 兼容双版本(`#if LVGL_VERSION_MAJOR >= 9` 分支):v8 挂 `disp_drv->rounder_cb`,v9 挂 `LV_EVENT_INVALIDATE_AREA` 事件回调,均为 2 像素对齐(起点向下取偶、终点向上取奇)——与 cubobox 的 rounder 相同。版本分布:**07/09 = LVGL 9.5,08 = LVGL 8.4**。
- 显示注册:`ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG`(无 PSRAM 场景),绘制缓冲高 **50 行(07/08)/ 100 行(09)**;触摸 indev 经 `esp_lv_adapter_register_touch` 注册;09 另有适配器任务调优(栈 20KB、优先级 10、绑核 0)。
- 并发模型:`Lvgl_lock(timeout)/Lvgl_unlock` 互斥锁保护跨任务 LVGL 访问(如亮度任务与 LVGL 自带任务并存);`esp_lv_adapter_start()` 启动适配器内部 LVGL 任务。
- 08/09 运行官方 `lv_demo_widgets()` 验证整链(屏+触摸+刷新)。

### 10. 文件存储(SPIFFS,07)

- 分区:`storage` 3M SPIFFS(**07/08/09 的 partitions.csv 均为 factory 6M + storage 3M;01-06 为 factory 8M 无数据分区**)。
- 挂载:`/spiffs`,失败自动格式化,max_files=2;PCM 资源在构建期打包进分区镜像随固件烧录。

## 官方引脚总表(以实际代码为准)

| 功能 | 引脚 | 依据 |
|------|------|------|
| I2C0 SCL / SDA | GPIO7 / GPIO8 | i2c_bsp 构造(全项目) |
| LCD QSPI PCLK | GPIO0 | display_bsp 默认参数 |
| LCD QSPI D0-D3 | GPIO1-4 | 同上 |
| LCD CS | **GPIO15** | 同上(实际生效;user_config.h 的 5 为过时宏) |
| 触摸 INT / RST | **GPIO5 / GPIO11** | 同上(实际生效;user_config.h 的 15 为过时宏) |
| TF 卡 CLK / MOSI / MISO / CS | GPIO0 / 1 / 2 / GPIO6 | sdcard_bsp 默认参数 |
| I2S MCLK / BCLK / WS / DIN / DOUT | GPIO19 / 20 / 22 / 21 / 23 | board_cfg.txt(C6_AMOLED_2_16) |
| 屏电源/复位 | AXP2101 ALDO3 | power_bsp + display_bsp |
| 背光 | SH8601 命令 0x51(无 GPIO) | display_bsp |
| BOOT 按键 | GPIO9(硬件存在,官方源码未使用) | — |

> LCD 与 TF 卡共享 CLK(0)/MOSI(1)/MISO(2):官方通过"示例分离"(04 用卡、07/08/09 用屏)回避冲突,未提供运行时总线切换。

## 与 cubobox 固件的对照

| 维度 | 官方示例 | cubobox |
|------|----------|---------|
| AXP2101 | XPowersLib,DC1+ALDO1-4 全开 3.3V,充电 500mA | 手写寄存器,同样轨配置;未配充电参数 |
| LCD PCLK | 40MHz(queue depth 1) | 10MHz(queue depth 10) |
| 触摸方向 | swap_xy=1, mirror_y=1 | bsp_touch 已删(重置);参考官方参数 |
| LVGL 移植 | esp_lvgl_adapter 组件 | 自写 flush_cb + tick 定时器 |
| LVGL 上下文 | 互斥锁 + 适配器任务 | 仅 gui_task 上下文(无锁) |
| TF/屏共存 | 示例分离,无切换逻辑 | 开机拉高 TF CS 释放总线 |
| 分区 | 01-06:factory 8M;07/08/09:factory 6M + storage 3M | factory 6M(storage 已删) |

## 环境备注

- 官方目录名 `ESP-IDF-v5.5.3`,`sdkconfig.defaults` 生成注释为 **5.5.1(01-06)/ 5.5.2(07/08/09)**,组件清单只约束 `idf>=4.1.0`;核心依赖实际版本:**lvgl 9.5.0(07/09)、8.4.0(08)**、`esp_lcd_sh8601 1.0.0`(cubobox 用 2.0.1)、`esp_lcd_touch_cst9217 1.0.4`、`esp_lvgl_adapter 0.3.0`、`qmi8658 1.0.1`、`pcf85063a 1.1.1`。
- 语音助手能力(麦克风阵列 + WiFi 对话)在仓库 `02_Example/XiaoZhi-v2.2.5/` 中实现,同样不含蓝牙。
