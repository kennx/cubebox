/*
 * KEY 用户按键 HAL：GPIO10 输入 + 内部上拉，低电平有效（原理图 Key3，
 * 无外部上拉）。轮询接口——沿检测/消抖语义由 low_power 状态机持有，
 * 10ms 轮询粒度下机械抖动仅表现为短暂的电平翻转，不产生伪唤醒路径。
 */
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>

#include "hal/hal_button.h"

#define TAG "hal_button"

#define BUTTON_GPIO 10 /* KEY 用户按键（官方原理图 Key3；BOOT=GPIO9 不用） */

void hal_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "KEY button (GPIO %d) init failed: %s, wake-by-key disabled",
                 BUTTON_GPIO, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "KEY button ready on GPIO %d (active low, internal pull-up)", BUTTON_GPIO);
    }
}

bool hal_button_pressed(void)
{
    return gpio_get_level(BUTTON_GPIO) == 0;
}
