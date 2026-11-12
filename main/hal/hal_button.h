#pragma once

#include <stdbool.h>

/**
 * @brief 初始化 KEY 用户按键（GPIO 10 输入 + 内部上拉）
 * @note 原理图 Key3：无外部上拉，按下接地 → 低电平有效。GPIO10 现无其他占用
 *       （引脚来源：官方原理图解析，见 docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md）。
 *       失败仅 ESP_LOGE，不致命（低功耗仍可经触摸唤醒）。
 */
void hal_button_init(void);

/**
 * @brief KEY 当前是否按下（低电平=按下）
 * @note 轮询接口（gui_task 10ms 粒度足够），沿检测/消抖语义由调用方（low_power）持有
 */
bool hal_button_pressed(void);
