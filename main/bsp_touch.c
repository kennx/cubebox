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

/* 触摸方向：与原厂 DisplayPort_TouchInit 一致（swap_xy=1, mirror_y=1） */
#define TP_SWAP_XY      1
#define TP_MIRROR_X     0
#define TP_MIRROR_Y     1

#define TP_RST_GPIO     11
#define TP_INT_GPIO     5

static esp_lcd_touch_handle_t s_tp = NULL;
static lv_indev_drv_t s_indev_drv;

static volatile uint32_t s_last_activity_ms = 0;
static volatile bool s_swallow_once = false;

/* 自研滑动检测：检测在 read_cb 内置位，动作由 gui_task 主循环消费 */
#define SWIPE_THRESHOLD_PX  80
static volatile lv_dir_t s_pending_swipe = 0;
static bool s_swipe_press_active = false;
static int s_swipe_start_x = 0;
static int s_swipe_start_y = 0;
static bool s_swipe_fired = false;

lv_dir_t bsp_touch_consume_swipe(void)
{
    lv_dir_t dir = s_pending_swipe;
    s_pending_swipe = 0;
    return dir;
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    esp_lcd_touch_point_data_t pts[1] = {0};
    uint8_t points = 0;

    /* 必须先触发 I2C 读取：get_data 只返回缓存（tp->data），
     * 不调 read_data 的话 points 永远为 0（原厂 esp_lvgl_adapter 同款两步模式） */
    esp_lcd_touch_read_data(tp);
    bool raw_pressed = (esp_lcd_touch_get_data(tp, pts, &points, 1) == ESP_OK) && points > 0;
    uint16_t x = pts[0].x;
    uint16_t y = pts[0].y;

    /* 自研滑动检测：用原始按压流判定（唤醒吞点击不影响滑动） */
    if (raw_pressed) {
        if (!s_swipe_press_active) {
            s_swipe_press_active = true;
            s_swipe_start_x = x;
            s_swipe_start_y = y;
            s_swipe_fired = false;
        } else if (!s_swipe_fired) {
            int dx = (int)x - s_swipe_start_x;
            int dy = (int)y - s_swipe_start_y;
            if (dx > SWIPE_THRESHOLD_PX || dx < -SWIPE_THRESHOLD_PX ||
                dy > SWIPE_THRESHOLD_PX || dy < -SWIPE_THRESHOLD_PX) {
                lv_dir_t dir;
                if (dx > 0 && dx >= (dy < 0 ? -dy : dy)) dir = LV_DIR_RIGHT;
                else if (dx < 0 && -dx >= (dy < 0 ? -dy : dy)) dir = LV_DIR_LEFT;
                else if (dy > 0) dir = LV_DIR_BOTTOM;
                else dir = LV_DIR_TOP;
                s_swipe_fired = true;
                ESP_LOGI(TAG, "swipe dir=%d (1=L 2=R 4=T 8=B)", dir);
                s_pending_swipe = dir; /* 动作由 gui_task 消费，绝不在此调 LVGL */
            }
        }
    } else {
        s_swipe_press_active = false;
    }
    bool pressed = raw_pressed;

    if (pressed) {
        s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        ESP_LOGD(TAG, "touch (%u, %u)", x, y); /* 高频采样日志，仅 DEBUG 级 */
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
    /* 触摸板实测点击抖动 10-16px，LVGL 默认 scroll_limit=10 会把点击全判成滚动
     * （列表行/滑条点不动）；放宽到 30px，大幅滑动仍正常滚动列表 */
    s_indev_drv.scroll_limit = 30;
    lv_indev_t *indev = lv_indev_drv_register(&s_indev_drv);
    assert(indev != NULL);
    ESP_LOGI(TAG, "LVGL pointer indev registered (scroll_limit=30)");
}

uint32_t bsp_touch_last_activity_ms(void)
{
    return s_last_activity_ms;
}

void bsp_touch_set_swallow_once(void)
{
    s_swallow_once = true;
}

void bsp_touch_debug_poll(int seconds)
{
    if (s_tp == NULL) {
        ESP_LOGE(TAG, "TPTEST: touch not init");
        return;
    }
    int touched = 0, no_ack = 0, i2c_fail = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(seconds * 1000);
    ESP_LOGI(TAG, "TPTEST: polling %ds, touch the screen NOW", seconds);
    while (xTaskGetTickCount() < end) {
        esp_err_t r = esp_lcd_touch_read_data(s_tp);
        esp_lcd_touch_point_data_t p[1] = {0};
        uint8_t n = 0;
        esp_lcd_touch_get_data(s_tp, p, &n, 1);
        if (r == ESP_OK) {
            if (n > 0) {
                touched++;
                ESP_LOGI(TAG, "TPTEST point (%d, %d)", p[0].x, p[0].y);
            }
        } else if (r == ESP_ERR_INVALID_RESPONSE) {
            no_ack++;
        } else {
            i2c_fail++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "TPTEST done: touched=%d no_ack=%d i2c_fail=%d", touched, no_ack, i2c_fail);
}
