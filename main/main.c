#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include "lvgl.h"
#include "bsp_power.h"
#include "bsp_rtc.h"
#include "bsp_lcd.h"
#include "ui_clock.h"

LV_FONT_DECLARE(lv_font_montserrat_48);

#define TAG "main"

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0

/*
 * 时间同步任务：通过 USB-Serial/JTAG（辅助 console）接收
 * "SETTIME YYYY-MM-DD HH:MM:SS" 命令，把电脑本地时间写入 PCF85063 RTC。
 */
static void time_sync_task(void *arg)
{
    usb_serial_jtag_driver_config_t jtag_cfg = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 4096, /* 必须大于单次写入长度（截图行 ~1945B），驱动环形缓冲整段写入是 all-or-nothing */
    };
    if (usb_serial_jtag_driver_install(&jtag_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "USB-Serial/JTAG driver install failed, time sync disabled");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "time_sync ready: send 'SETTIME YYYY-MM-DD HH:MM:SS' over USB");

    char line[64];
    size_t len = 0;
    uint8_t ch;
    while (1) {
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) <= 0) {
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[len] = '\0';
            len = 0;

            int y, mo, d, h, mi, s;
            if (sscanf(line, "SETTIME %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6 &&
                y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
                h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59) {
                esp_err_t ret = bsp_rtc_set_datetime(y % 100, mo, d, bsp_rtc_day_of_week(y, mo, d), h, mi, s);
                if (ret == ESP_OK) {
                    uint8_t hh, mm, ss, dd, wd, mo2, yy2;
                    if (bsp_rtc_get_datetime(&hh, &mm, &ss, &dd, &wd, &mo2, &yy2) == ESP_OK) {
                        ESP_LOGI(TAG, "RTC SET OK: %s 20%02d-%02d-%02d %02d:%02d:%02d",
                                 bsp_rtc_weekdays_str[wd], yy2, mo2, dd, hh, mm, ss);
                    }
                } else {
                    ESP_LOGE(TAG, "RTC set failed: %s", esp_err_to_name(ret));
                }
            } else if (strcmp(line, "SNAP") == 0) {
                bsp_lcd_snapshot_arm();
            }
            continue;
        }
        if (len < sizeof(line) - 1) {
            line[len++] = (char)ch;
        }
    }
}

static void gui_task(void *arg)
{
    /* Set screen background to black */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ui_clock_init(scr);

    uint32_t last_rtc_update = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Update RTC time and date every 1000ms */
        if (now - last_rtc_update >= 1000 || last_rtc_update == 0) {
            ui_clock_tick_refresh();
            last_rtc_update = now;
        }

        /* 截图请求：强制全屏 invalidate，让 flush_cb 流出全部条带 */
        if (bsp_lcd_snapshot_begin_if_requested()) {
            lv_obj_invalidate(lv_scr_act());
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    /* Pull up TF card CS (GPIO 6) to disable TF card and release the shared SPI/QSPI bus lines */
    gpio_config_t tf_cs_cfg = {
        .pin_bit_mask = 1ULL << 6,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&tf_cs_cfg);
    gpio_set_level(6, 1);
    ESP_LOGI("main", "TF Card CS (GPIO 6) pulled high to release shared SPI bus");

    /* Shared I2C bus: AXP2101 / PCF85063 / 触摸（后续） */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    bsp_power_init(i2c_bus);
    bsp_rtc_init(i2c_bus);
    bsp_lcd_init();         /* Initialize LCD + LVGL（内部先做屏电源复位） */

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
    xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 5, NULL);
}
