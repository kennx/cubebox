#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "lvgl.h"
#include "bsp_touch.h"

#define TAG "bsp_touch"

/* 触摸方向标定宏：真机画线测试后按需翻转（0/1） */
#define TP_SWAP_XY      0
#define TP_MIRROR_X     0
#define TP_MIRROR_Y     0

#define TP_RST_GPIO     11
#define TP_INT_GPIO     5

static esp_lcd_touch_handle_t s_tp = NULL;
static lv_indev_drv_t s_indev_drv;

static volatile uint32_t s_last_activity_ms = 0;
static volatile bool s_swallow_once = false;

/* 前几次触摸坐标打日志，便于方向标定 */
static int s_touch_log_count = 0;

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    esp_lcd_touch_point_data_t pts[1] = {0};
    uint8_t points = 0;

    bool pressed = (esp_lcd_touch_get_data(tp, pts, &points, 1) == ESP_OK) && points > 0;
    uint16_t x = pts[0].x;
    uint16_t y = pts[0].y;

    if (pressed) {
        s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_touch_log_count < 10) {
            ESP_LOGI(TAG, "touch (%u, %u)", x, y);
            s_touch_log_count++;
        }
    }

    /* 低功耗唤醒：整段按压都不上报（indev 30ms 周期采样，人手按压 ≥100ms，
     * 只吞第一个采样挡不住误触），抬起后才恢复正常 */
    if (s_swallow_once) {
        if (pressed) {
            pressed = false;
        } else {
            s_swallow_once = false;
        }
    }

    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void bsp_touch_init(i2c_master_bus_handle_t bus)
{
    /* cst9217 组件接口基于 esp_lcd panel io：先把 i2c 总线包装成 panel io */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 100000;
    esp_lcd_panel_io_handle_t tp_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(bus, &io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 480,
        .y_max = 480,
        .rst_gpio_num = TP_RST_GPIO,
        .int_gpio_num = TP_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = TP_SWAP_XY,
            .mirror_x = TP_MIRROR_X,
            .mirror_y = TP_MIRROR_Y,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &s_tp));
    ESP_LOGI(TAG, "CST9217 touch panel created (swap_xy=%d mirror_x=%d mirror_y=%d)",
             TP_SWAP_XY, TP_MIRROR_X, TP_MIRROR_Y);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    s_indev_drv.user_data = s_tp;
    lv_indev_t *indev = lv_indev_drv_register(&s_indev_drv);
    assert(indev != NULL);
    ESP_LOGI(TAG, "LVGL pointer indev registered");
}

uint32_t bsp_touch_last_activity_ms(void)
{
    return s_last_activity_ms;
}

void bsp_touch_set_swallow_once(void)
{
    s_swallow_once = true;
}
