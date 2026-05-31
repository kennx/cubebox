# 2.16 英寸 AMOLED 居中时钟显示设计规格 (Design Spec)

## 1. 概述 (Overview)
本项目将使用 LVGL 8.3 框架在 ESP32-C6-Touch-AMOLED-2.16 开发板的 `480x480` AMOLED 屏幕上实现一个水平与垂直方向均居中的数字时钟。
时间每秒更新一次，从板载的 PCF85063 硬件 RTC 芯片读取，并以 24 小时制 `HH:MM:SS` 格式显示。
根据用户选择：
1. 使用 **LVGL (选项 B)** 进行界面排版，以达到高级且精准居中的显示效果。
2. 采用 **直接读取 RTC 原始状态 (选项 C)** 的方案，启动时不写入或修改 RTC 时间，只进行纯读取。

---

## 2. 硬件规格与接口 (Hardware Interfaces)
* **主控芯片**：ESP32-C6 (HP SRAM 512KB)
* **AMOLED 屏幕**：2.16 英寸，分辨率 `480x480`
  * 驱动 IC: CO5300 (兼容 SH8601)，接口：4-bit QSPI。
  * 像素格式：RGB565，大端对齐（Big-Endian，传输时需进行字节交换）。
  * 屏幕背光调节：通过发送命令 `0x51` 控制亮度，已由 PMIC 驱动。
* **PMIC (电源管理)**：AXP2101
  * ALDO3 (0x90 寄存器的 bit 2) 充当屏幕的电源复位引脚。
  * I2C 总线 0 (SDA=GPIO 8, SCL=GPIO 7) 已初始化。
* **RTC 实时时钟**：PCF85063
  * I2C 物理挂载在同一个 I2C 总线 0，从机地址为 `0x51`。
  * 时间读取寄存器：
    * `0x04` (Seconds): 范围 `00-59` BCD 格式。bit 7 是 OS (Oscillator Stop) 标志。
    * `0x05` (Minutes): 范围 `00-59` BCD 格式。
    * `0x06` (Hours): 范围 `00-23` BCD 格式 (默认 24 小时制模式)。

---

## 3. 软件架构设计 (Software Architecture)

### 3.1 内存极低开销 LVGL 缓冲方案
由于 ESP32-C6 HP SRAM 仅有 512KB，除去系统与 IDF 基础开销，可用内存紧张。因此，我们采用局部单缓冲区方案：
* 缓冲区高度设计为 40 像素（占屏幕的 1/12）。
* 缓冲区尺寸：`480 * 40 * sizeof(lv_color_t)`（RGB565 模式下 `sizeof(lv_color_t) = 2`），总大小为 `38,400` 字节（约 37.5KB）。
* 缓冲区从 DMA 可访问的内部 SRAM 堆中动态分配，确保 QSPI 传输稳定性。

### 3.2 LVGL 心跳与定时器泵
* **心跳（LVGL Tick）**：注册一个 `esp_timer` 软件定时器，周期为 2ms，在中断/回调中安全调用 `lv_tick_inc(2)`。
* **处理泵与 UI 任务（UI Task）**：创建一个独立的 FreeRTOS 任务 `gui_task`，每隔 10~30ms 运行一次 `lv_timer_handler()`。
* **RTC 时间采集与 UI 刷新**：为了规避多线程加锁对 LVGL 造成的死锁风险，我们不创建额外的读取任务。直接在 `gui_task` 中以 1000ms 为周期定时调用 `pcf85063_get_time` 读取 RTC，并将格式化好的字符串通过 `lv_label_set_text` 渲染到中心 label。

### 3.3 PCF85063 I2C 裸机驱动
* 使用 ESP-IDF 6.0 的新 master i2c 驱动句柄 `i2c_master_dev_handle_t`，通过挂载到已初始化的 `i2c_bus` 实现从机通信。
* 从 `0x04` 开始读取 3 字节，使用 `bcd2dec` 转换器解析 `HH:MM:SS`。

---

## 4. UI 界面与视觉设计 (User Interface Design)
* **背景色**：使用深沉的纯黑背景 (`0x0000`)。
* **时钟文本**：
  * 使用 LVGL 自带的大字号字体 `lv_font_montserrat_48`，保证视觉上大气醒目。
  * 文本颜色采用炫目的明黄色 (`0xFFE0`)，以保证在 AMOLED 屏幕上的高对比度与高端感。
* **对齐方式**：
  * 创建一个全屏容器或直接在主屏幕 (`lv_scr_act()`) 上创建 label。
  * 调用 `lv_obj_center(label)`，并在 label 属性中设置文本水平居中 `lv_label_set_recolor(label, true)` 和居中对齐，从而使得时间 `HH:MM:SS` 完美在屏幕中心水平与垂直居中。

---

## 5. 编译与运行配置 (Compilation Requirements)
* 在 `main/idf_component.yml` 中注入 `"espressif/lvgl": "^8.3.11"` 依赖。
* 确保 `sdkconfig` 中包含 LVGL 的基础 Kconfig 选项（如使能 `lv_font_montserrat_48`）。
