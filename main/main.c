#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include "lvgl.h"
#include "hal/hal.h"
#include "apps/apps.h"
#include "apps/common/swipe_nav/swipe_nav.h"
#include "apps/app_clock/app_clock.h"
#include "apps/app_test/app_test.h"

#define TAG "main"

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0

/*
 * USB console：接收调试命令。主 console 即 USB-Serial/JTAG（sdkconfig
 * 配置，ESP_LOG 与命令/截图流共用同一通道）；此处经 usb_serial_jtag
 * 驱动直读，绕开 console 子系统的行缓冲，读取不受日志输出干扰。
 *   SNAP   — 触发一次全屏截图流（配合 tools/screenshot.py）
 *   NEXT   — 循环切换到下一个已安装应用（触摸失效时的兜底验证途径）
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
                hal_display_snapshot_arm();
            } else if (strcmp(line, "NEXT") == 0) {
                swipe_nav_step(1); /* 循环切下一个已安装应用（触摸失效时的兜底） */
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

/*
 * gui_task：全固件唯一允许调用 LVGL 的上下文。每循环：
 *   1. 截图请求 → 强制全屏 invalidate（flush_cb 流出全部条带）
 *   2. apps_update：消费跨任务的应用切换请求 + 调当前应用 on_running
 *   3. lv_timer_handler 驱动 LVGL 刷新
 */
static void gui_task(void *arg)
{
    (void)arg;
    while (1) {
        if (hal_display_snapshot_begin_if_requested()) {
            lv_obj_invalidate(lv_scr_act());
        }

        apps_update();

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    /* 时区 UTC+8：app_clock 按本地时间走秒（系统时钟未同步时开机 08:00 起，
     * RTC/SNTP 同步为后续独立工作） */
    setenv("TZ", "CST-8", 1);
    tzset();

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

    /* HAL：先电源后显示（显示复位依赖 ALDO3），再触摸（失败不致命） */
    hal_power_init(i2c_bus);
    hal_display_init();
    hal_touch_init(i2c_bus);

    /* 安装应用（安装顺序即左右滑动的切换顺序），默认启动点阵时钟 */
    if (!apps_install(&app_clock)) {
        ESP_LOGE(TAG, "apps_install(app_clock) failed (table full or duplicate)");
    }
    if (!apps_install(&app_test)) {
        ESP_LOGE(TAG, "apps_install(app_test) failed (table full or duplicate)");
    }
    apps_request_open(app_clock.name);

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
}
