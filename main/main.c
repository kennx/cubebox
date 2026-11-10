#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include "lvgl.h"
#include "bsp_power.h"
#include "bsp_lcd.h"

LV_FONT_DECLARE(lv_font_montserrat_48);

#define TAG "main"

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0

/*
 * USB 辅助 console：接收调试命令。主 console 是 UART0，stdin 收不到
 * USB 数据，必须经 usb_serial_jtag 驱动读取。
 *   SNAP   — 触发一次全屏截图流（配合 tools/screenshot.py）
 *   REBOOT — 远程重启（抓完整启动日志用）
 */
static void console_task(void *arg)
{
    usb_serial_jtag_driver_config_t jtag_cfg = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 4096, /* 必须大于单次写入长度（截图行 ~1945B），驱动环形缓冲整段写入是 all-or-nothing */
    };
    if (usb_serial_jtag_driver_install(&jtag_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "USB-Serial/JTAG driver install failed, console disabled");
        vTaskDelete(NULL);
        return;
    }

    char line[16];
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
            if (strcmp(line, "SNAP") == 0) {
                bsp_lcd_snapshot_arm();
            } else if (strcmp(line, "REBOOT") == 0) {
                esp_restart();
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
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "OK");
    lv_obj_center(label);

    while (1) {
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
    ESP_LOGI(TAG, "TF card CS (GPIO 6) pulled high to release shared SPI bus");

    /* Shared I2C bus: AXP2101（屏幕供电/复位依赖 ALDO3） */
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
    bsp_lcd_init(); /* Initialize LCD + LVGL（内部先经 ALDO3 做屏电源复位） */

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
}
