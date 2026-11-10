/*
 * CST9220 触摸 HAL：经 waveshare/esp_lcd_touch_cst9217 组件驱动（板上触控
 * IC 标称 CST9220，官方示例以 CST9217 库驱动，I2C 0x5A），并向 LVGL 注册
 * 指针输入设备。方向参数（swap_xy+mirror_y）与 Waveshare 官方 07/08/09
 * 示例实证一致。
 */
#include <esp_err.h>
#include <esp_log.h>

#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "lvgl.h"

#include "hal/hal_touch.h"

#define TAG "hal_touch"

/* 总线与引脚参数（官方示例实证值） */
#define TOUCH_I2C_SCL_SPEED_HZ 400000
#define TOUCH_GPIO_RST         11
#define TOUCH_GPIO_INT         5

/* 连续读取失败上限（约 1.5s @30ms 读周期）：组件 err 路径含 RST 复位 +
 * vTaskDelay(约 60ms)，持续故障会拖垮 gui_task 的 30ms 节奏——超限即
 * 停用触摸（一次性降级，重启恢复），保住 UI 主循环。 */
#define TOUCH_READ_FAIL_LIMIT 50

static esp_lcd_touch_handle_t s_touch = NULL;
static uint8_t s_read_fail_cnt = 0;

/* LVGL v8 指针输入读取回调（gui_task 的 lv_timer_handler 内周期调用）。
 * 无触摸点按"本轮无触摸"处理；I2C 读取失败计数降级（见 FAIL_LIMIT）。 */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    LV_UNUSED(drv);
    data->state = LV_INDEV_STATE_REL;
    if (s_touch == NULL) {
        return;
    }
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        if (++s_read_fail_cnt >= TOUCH_READ_FAIL_LIMIT) {
            ESP_LOGW(TAG, "touch read failed %u times, touch disabled until reboot",
                     s_read_fail_cnt);
            s_touch = NULL; /* 放弃句柄（组件 err 路径状态未定，不做清理） */
        }
        return;
    }
    s_read_fail_cnt = 0;
    esp_lcd_touch_point_data_t points[1];
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(s_touch, points, &cnt, 1) == ESP_OK && cnt > 0) {
        data->point.x = points[0].x;
        data->point.y = points[0].y;
        data->state = LV_INDEV_STATE_PR;
    }
}

void hal_touch_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        ESP_LOGE(TAG, "i2c bus is NULL, touch disabled");
        return;
    }

    /* 在共享 I2C 总线上挂触摸 panel io（0x5A@400kHz） */
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_config.scl_speed_hz = TOUCH_I2C_SCL_SPEED_HZ;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err = esp_lcd_new_panel_io_i2c_v2(bus, &io_config, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch panel io create failed: %s, touch disabled",
                 esp_err_to_name(err));
        return;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 480,
        .y_max = 480,
        .rst_gpio_num = TOUCH_GPIO_RST,
        .int_gpio_num = TOUCH_GPIO_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 1, .mirror_y = 1 },
    };
    err = esp_lcd_touch_new_i2c_cst9217(io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CST9220 probe failed: %s, touch disabled", esp_err_to_name(err));
        s_touch = NULL;
        return;
    }

    /* LVGL v8 驱动结构体注册（hal_display 同代 API） */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    if (lv_indev_drv_register(&indev_drv) == NULL) {
        ESP_LOGE(TAG, "lv_indev_drv_register failed, touch disabled");
        return;
    }
    ESP_LOGI(TAG, "CST9220 touch ready (swap_xy+mirror_y)");
}
