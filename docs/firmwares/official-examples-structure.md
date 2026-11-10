# Waveshare 官方 ESP-IDF 示例目录结构(ESP32-C6-Touch-AMOLED-2.16)

> 来源:[waveshareteam/ESP32-C6-Touch-AMOLED-2.16](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16) 的 `02_Example/ESP-IDF-v5.5.3/`。
> 目录名标注 ESP-IDF v5.5.3;`sdkconfig.defaults` 由 `save-defconfig` 生成(01-06 注释 5.5.1、07/08/09 注释 5.5.2),`idf_component.yml` 只要求 `>=4.1.0`。
> 本文记录目录结构与每个文件的作用;设备能力详情见 [official-examples-capabilities.md](./official-examples-capabilities.md)。

## 仓库顶层

```
ESP32-C6-Touch-AMOLED-2.16/
├── 01_Arduino_Libraries/     # Arduino 库与示例(不在本文范围)
├── 02_Example/
│   ├── Arduino-v3.3.3/       # Arduino 示例(不在本文范围)
│   ├── ESP-IDF-v5.5.3/       # 本文研究对象:9 个独立 ESP-IDF 项目
│   └── XiaoZhi-v2.2.5/       # 小智 AI 语音助手固件(不在本文范围)
├── 03_Firmware/              # 预编译固件(不在本文范围)
├── README.md                 # 仅 wiki 链接 + 更新日志(1.0.0, 2026-03-30 初始版)
└── Tools Configuration.png   # Arduino IDE 工具链配置截图
```

## ESP-IDF-v5.5.3/ 项目总览

9 个**相互独立**的 ESP-IDF 项目,按外设由浅入深排列,每个可单独打开、编译、烧录:

| 项目 | 演示能力 | 特有组件 |
|------|----------|----------|
| `01_AXP2101_Test` | 电源管理(充电状态/电池电压轮询) | — |
| `02_I2C_QMI8658` | IMU 六轴(加速度+陀螺仪) | — |
| `03_I2C_PCF85063` | RTC 实时时钟(设置/读取时间) | — |
| `04_SD_Card` | TF 卡(SDSPI + FAT 文件读写) | `port_bsp/sdcard_bsp.*` |
| `05_WIFI_STA` | WiFi Station 模式联网 | — |
| `06_WIFI_AP` | WiFi SoftAP 热点 | — |
| `07_Audio_Test` | 音频录音/回放 + SPIFFS + LVGL UI | `port_bsp/codec_bsp.*`、`ExternLib/codec_board/`、`ui_bsp/` |
| `08_LVGL_V8_Test` | LVGL v8 + 触摸(widgets demo) | — |
| `09_LVGL_V9_Test` | LVGL v9 + 触摸(与 08 同一 main.cpp) | — |

**跨项目复用关系**(经逐文件 diff 确认):

- `components/pmicpower/`(XPowersLib 电源库 + 板级 `power_bsp`):9 个项目逐字节相同,仅 `power_bsp.cpp` 中轮询打印间隔不同(01=2s,其余=20s)。
- `components/port_bsp/i2c_bsp.{cpp,h}`:9 个项目完全相同。
- `components/port_bsp/display_bsp.{cpp,h}`:07/08/09 三个项目完全相同。
- `components/app_bsp/lvgl_bsp.{cpp,h}`:07 与 08 相同;**09 不同**(任务栈/优先级/绑核调优 + 缓冲 100 行,见 app_bsp 小节)。
- 08 与 09 的 `main/main.cpp` 完全相同(同一份代码分别链接 LVGL 8.4 / 9.5)。

## 单个项目的标准结构(以 08 为例)

```
08_LVGL_V8_Test/
├── CMakeLists.txt              # 项目入口:cmake_minimum_required + project()
│                               #   ※ 07 另有 set(EXTRA_COMPONENT_DIRS components/ExternLib)
├── partitions.csv              # 分区表:nvs(0x6000)/phy_init(0x1000) + factory app
│                               #   ※ 01-06:factory 8M、无数据分区;07/08/09:factory 6M + storage(3M SPIFFS)
├── sdkconfig.defaults          # Kconfig 默认值(见下文"配置要点")
├── main/
│   ├── CMakeLists.txt          # 只注册 main.cpp(依赖组件经 idf_component.yml 拉取)
│   ├── idf_component.yml       # 组件管理器清单(sh8601/lvgl/cst9217/esp_lvgl_adapter…)
│   ├── main.cpp                # app_main 入口:初始化顺序 + 演示逻辑
│   └── user_config.h           # 全板引脚宏定义(注意:部分宏与实际代码不符,见能力文档)
└── components/
    ├── pmicpower/              # AXP2101 电源管理(见下文详表)
    ├── port_bsp/               # 外设板级适配(i2c/display/sdcard/codec)
    └── app_bsp/                # LVGL 移植适配(仅 07/08/09)
```

### `main/main.cpp` 各项目要点

| 项目 | app_main 流程 |
|------|---------------|
| 01 | `Custom_PmicPortInit` → 创建 `Axp2101_isChargingTask`(2s 轮询充电状态+电池电压) |
| 02 | PMIC init → `qmi8658_init` → 配置量程/ODR/单位 → `QMI8658_Task`(500ms 轮询 DRDY 后读六轴) |
| 03 | PMIC init → `pcf85063a_init` → 设 2026-01-01 08:00:00 → 每秒读时间打印 |
| 04 | PMIC init → `new CustomSDPort("/sdcard")`(挂载+打印卡信息,无进一步 IO) |
| 05 | PMIC init → NVS/netif/事件循环 → STA 模式(SSID `ESP32_STA`/`12345678`)→ 打印 IP |
| 06 | 同 05 → AP 模式(SSID `ESP32_AP`/`12345678`,WPA/WPA2,最多 4 连接)→ 打印接入 MAC |
| 07 | 挂载 SPIFFS → PMIC → Display+触摸 → CodecPort → LVGL UI → 音频任务(事件组驱动录音/回放/音乐) |
| 08/09 | PMIC → Display+触摸 → `Lvgl_PortInit` → `lv_demo_widgets()` → 亮度渐变测试任务(100→0→100,步进 20%,2s) |

### `main/user_config.h`(全板引脚宏)

每个项目都带同一份 `user_config.h`,集中定义 I2C/SPI/I2S/LCD/SD 的 GPIO 编号(完整引脚表见能力文档)。
**注意:全仓 grep 实际引用情况**——只有 `BSP_I2C_SCL/SDA/NUM`(各 main.cpp)与 `BSP_LCD_H_RES/V_RES`(07/08/09 的 lvgl_bsp.cpp)被真实使用;`BSP_LCD_CS=5`、`BSP_LCD_TOUCH_INT=15` 与 `display_bsp.cpp` 构造函数默认值(`cs=15, tp_int=5, tp_reset=11`)正好对调且未被引用,各 main 实例化 `DisplayPort` 不传参(用默认值),即实际生效 15/5/11;`BSP_I2S_*`、`BSP_SD_*` 宏同样零引用(音频引脚真源是 board_cfg.txt,SD 引脚真源是 sdcard_bsp 构造默认值)。

### `main/idf_component.yml` 依赖清单

| 项目 | 依赖 |
|------|------|
| 01/03/04/05/06 | (空清单——外部组件一个不用) |
| 02 | `waveshare/qmi8658 ^1.0.1`(注释掉 pcf85063a) |
| 03 | `waveshare/pcf85063a ^1.1.1` |
| 07 | `espressif/esp_lcd_sh8601 ^1.0.0` + `lvgl/lvgl ^9.5.0` + `waveshare/esp_lcd_touch_cst9217 ^1.0.4` + `espressif/esp_lvgl_adapter ^0.3.0` |
| 08 | 同上,但 `lvgl/lvgl ^8.4.0` |
| 09 | 同 07(`lvgl/lvgl ^9.5.0`) |

### `sdkconfig.defaults` 配置要点

公共条目(9 份相同):`esp32c6` 目标、16MB flash、自定义分区表、`FREERTOS_HZ=1000`、`-O2` 性能优化、montserrat 12/16 字体、IRAM 快速内存属性。其余分两组(01-06 内部完全一致;07/08/09 内部仅差下述 LVGL 项):

| 组 | 生成注释 | 特有条目 |
|----|----------|----------|
| 01-06 | ESP-IDF **5.5.1** | `LV_COLOR_16_SWAP`、64KB LVGL 堆、montserrat 20/24/26、四个 demo(widgets/benchmark/stress/music)——但这六个项目不跑 LVGL,属无效配置 |
| 07 | ESP-IDF **5.5.2** | 仅公共条目(自身 UI 不需要 demo;亦未开 16_SWAP) |
| 08/09 | ESP-IDF **5.5.2** | `LV_COLOR_16_SWAP`、`LV_COLOR_SCREEN_TRANSP`、64KB LVGL 堆、`LV_USE_DEMO_WIDGETS`(跑 widgets demo 所需) |

## `components/` 逐文件作用

### pmicpower/(9 个项目共用)

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | 注册组件(src 递归源码 + 三级 include);关键编译开关 `add_compile_definitions(XPOWERS_CHIP_AXP2101 CONFIG_XPOWERS_ESP_IDF_NEW_API)` 裁剪库到仅 AXP2101 + IDF 新 I2C API;依赖 port_bsp/driver |
| `power_bsp.h/.cpp` | **板级层(重点)**:`Custom_PmicPortInit()` 在共享 I2C 总线上以 0x34 挂 AXP2101,经 XPowersLib 初始化;`Custom_PmicRegisterInit()` 配置 DC1/ALDO1-4 = 3.3V、VBUS 限流 2A、预充 50mA/恒流 500mA/终止 50mA;`Axp2101_isChargingTask()` 轮询充电状态机与电池电压;`Axp2101_SetAldo2/3()` 供外部开关 ALDO2/ALDO3(屏电源复位用 ALDO3) |
| `src/XPowersLib.h`、`src/XPowersLibInterface.{hpp,cpp}` | 第三方库 [lewisxhe/XPowers](https://github.com/lewisxhe/XPowers) 的库接口层:读写适配、PMU 对象创建、中断参数映射 |
| `src/XPowersAXP2101.tpp` | AXP2101 芯片实现(轨电压/充电/中断/库仑计等寄存器封装,.tpp 为模板实现) |
| `src/XPowersAXP192.tpp`、`src/XPowersAXP202.tpp`、`src/PowersBQ25896.tpp`、`src/PowersSY6970.tpp`、`src/PowerDeliveryHUSB238.hpp` | 其他 PMIC 芯片实现(AXP192/202、BQ25896、SY6970、HUSB238)——本板不使用,系库自带(被编译开关裁剪) |
| `src/REG/*.h` | 各芯片寄存器地址/位定义常量表(AXP2101Constants.h 本板相关) |
| `src/XPowersParams.hpp`、`XPowersCommon.tpp`、`XPowersLib_Version.h` | 参数枚举、公共基类实现、版本号 |

### port_bsp/(各项目按需)

| 文件 | 出现于 | 作用 |
|------|--------|------|
| `CMakeLists.txt` | 全部 9 个 | 注册组件源码;01-06 版仅 i2c_bsp(依赖 driver/pmicpower/esp_timer);04 版加 sdcard_bsp(依赖 fatfs/sdmmc);07 版最全(i2c+display+codec,依赖 sh8601/cst9217/codec_board/fatfs 等,并以 `EMBED_FILES "./pcm/canon.pcm"` 把卡农 PCM 嵌进固件);08/09 版为 i2c+display |
| `i2c_bsp.h/.cpp` | 全部 9 个 | `I2cMasterBus` 类:封装 I2C 主总线创建(内建上拉、glitch=7)与寄存器式读写(带 5s/1s 超时与总线忙等待),`Get_I2cBusHandle()` 供其他驱动共享总线 |
| `display_bsp.h/.cpp` | 07/08/09 | `DisplayPort` 类:QSPI 总线 + SH8601 面板初始化(40MHz、RGB565、vendor init 序列)、ALDO3 高-低-高屏复位、CST9217 触摸初始化(INT=5/RST=11/400kHz/swap_xy+mirror_y)、背光命令 0x51(0-100%→0-255)、旋转命令 0x36 |
| `sdcard_bsp.h/.cpp` | 04 | `CustomSDPort` 类:SDSPI 1-bit(CLK=0/MOSI=1/MISO=2/CS=6,SPI2)挂 FAT 到 `/sdcard`;整文件读/写、偏移读/写、追加 API;含被注释的"复用 Display SPI host"旧构造(总线共享方案的历史痕迹) |
| `codec_bsp.h/.cpp` | 07 | `CodecPort` 类:基于 esp_codec_dev 框架,ES8311(0x18 扬声器)+ES7210(0x40 麦克风)双设备,I2S TDM;音量/增益、寄存器直读写、PCM 播放/录音、内置 canon.pcm 音乐任务与回声任务(读→即播) |

### app_bsp/(07/08/09)

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | 注册 lvgl_bsp.cpp;依赖 esp_lvgl_adapter/port_bsp/lvgl 与 **main**(反向依赖,取 main/user_config.h 的分辨率宏) |
| `lvgl_bsp.h/.cpp` | LVGL 移植层:基于 `esp_lvgl_adapter` 组件——初始化适配器、注册 SPI 显示 + 2 像素 rounder(v8 driver 回调 / v9 事件回调,同文件双版本兼容)+ 触摸 indev;`Lvgl_lock/unlock`(互斥锁)、`Lvgl_Refresh()` 立即刷新。**07/08 与 09 不同**:07/08 显示缓冲高 50 行、适配器任务用默认参数;09 增设任务调优(栈 20KB、优先级 10、绑核 0、栈不进 PSRAM)并把缓冲高提到 **100** 行(diff 实证) |

### ExternLib/codec_board/(仅 07,Waveshare 通用音频组件)

| 文件 | 作用 |
|------|------|
| `board_cfg.txt` | 多板配置表:三块板(S3_LCD_3_49、S3_RLCD_4_2、**C6_AMOLED_2_16**)的 I2C/I2S 引脚与编解码器型号;C6 板条目:i2c{8,7}、i2s{mclk:19,bclk:20,ws:22,din:21,dout:23}、out=ES8311(无独立 PA)、in=ES7210 |
| `cfg_parse.c`(声明在 `include/codec_board.h`) | 运行时解析 board_cfg.txt,按板名查询 I2C/I2S 引脚与 codec 配置 |
| `codec_board.c` | 板级配置数据结构与查询接口(`set_codec_board_type`/`get_i2c_pin`/`get_i2s_pin` 等) |
| `codec_init.c`、`include/codec_init.h` | esp_codec_dev 框架初始化:I2C 主总线 + I2S 通道(STD/TDM/PDM 三模式按需)创建,装配 data/gpio/ctrl/codec 四层接口,产出 playback/record 两个 `esp_codec_dev` 句柄 |
| `dummy_codec.c/.h` | 无编解码芯片时的哑设备实现(框架占位,本板不用) |
| `drv/tca9554.c/.h` | TCA9554 I2C GPIO 扩展驱动(供其他板控制 PA/背光,C6 板配置未使用) |
| `lcd_init.c` | 组件自带的 ST7789/MIPI LCD 初始化(供 S3/P4 板,C6 项目未调用——显示走自己的 display_bsp+sh8601) |
| `CMakeLists.txt`、`idf_component.yml`、`.component_hash` | 组件构建登记与依赖声明(esp_codec_dev 等) |
| `README.md`、`CHANGELOG.md`、`LICENSE` | 组件说明/变更日志/许可 |

### ui_bsp/(仅 07,NXP GUI Guider 生成的 UI)

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | `GLOB_RECURSE` 收全部生成源码,依赖 lvgl,导出 custom/generated/guider_customer_fonts 三个 include 目录 |
| `generated/gui_guider.{c,h}` | UI 全局结构 `lv_ui`(screen + 4 按钮 btn_4~7 + 2 label)与 `setup_ui()` 入口 |
| `generated/setup_scr_screen.c` | 构建主屏:按钮文案 Recording / Play Recording / Play Music / Stop Music,标题 "Audio Test",状态 label |
| `generated/widgets_init.{c,h}`、`events_init.{c,h}` | 控件与事件桩(GUI Guider 代码生成骨架,实际事件在 main.cpp 里 `lv_obj_add_event_cb` 挂) |
| `generated/guider_fonts/` | montserrat Medium 12/16/18 三档字体的 C 数组实现(lv_font_montserratMedium_{12,16,18}.c + 汇总头) |
| `generated/guider_customer_fonts/guider_customer_fonts.h` | 自定义字体声明头(仅空声明,未放字体) |
| `generated/images/images_md5.json` | 图片资源清单(空 `{}`,未用图片) |
| `custom/custom.{c,h}`、`custom/lv_conf_ext.h` | 用户自定义扩展区(空实现) |

### 数据文件

| 文件 | 作用 |
|------|------|
| `07 port_bsp/pcm/canon.pcm` | 卡农 PCM(经 CMake `EMBED_FILES` 打进固件,CodecPort 音乐任务循环播放,24kHz/2ch/16bit) |
| `07 main/pcm/mood.pcm` | 背景音乐 PCM,经 `spiffs_create_partition_image(storage ./pcm FLASH_IN_PROJECT)` 烧进 SPIFFS,UI "Play Music" 按钮播放 |
