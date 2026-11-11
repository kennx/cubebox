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
#include <esp_app_desc.h>
#include "lvgl.h"
#include "hal/hal.h"
#include "config_store.h"
#include "time_sync.h"
#include "apps/apps.h"
#include "apps/common/swipe_nav/swipe_nav.h"
#include "apps/app_clock/app_clock.h"
#include "apps/app_test/app_test.h"
#include "apps/app_radius/app_radius.h"

#define TAG "main"

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0

/*
 * USB console：接收调试命令。主 console 即 USB-Serial/JTAG（sdkconfig
 * 配置，ESP_LOG 与命令/截图流共用同一通道）；此处经 usb_serial_jtag
 * 驱动直读，绕开 console 子系统的行缓冲，读取不受日志输出干扰。
 *   SNAP            — 触发一次全屏截图流（配合 tools/screenshot.py）
 *   NEXT            — 循环切换到下一个已安装应用（触摸失效时的兜底验证途径）
 *   RAD <px>        — 设置 app_radius 可调单弧半径（1-240）并切到该页
 *   PAGE <n>        — app_radius 直接跳页 0 白屏/1 刻度/2 可调弧
 *   REBOOT          — 远程重启（抓完整启动日志用）
 *   PING            — 配对握手：回 #HELLO {name,fw,proto}（网页配置器用）
 *   GETCFG          — 回 #CFG {brightness,tz,wifi_ssid,wifi_pass_set,ntp_server}
 *   SETCFG {json}   — 部分更新配置：回 #CFG {新状态} 或 #ERR {原因}
 *   SCAN            — 阻塞扫描附近未隐藏 AP（~2-4s）：回 #NETWORKS [{ssid,rssi,secure}]
 *   TIME            — 回 #TIME {synced,utc,local}（网页时钟页显示用）
 * 协议响应统一 # 前缀，与 ESP_LOG 行区分（规格见 docs/features/web-config.md）。
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

    /* 行缓冲覆盖最大合法 SETCFG 载荷（~130B）；超长行截断 → JSON 解析失败 → #ERR。
     * GETCFG/SETCFG 成功时把 json 复用 line 缓冲（此时命令已消费完毕）。
     * resp 容纳 #NETWORKS（16 条 AP × 最坏 ~100B ≈ 1.2KB）< USB tx 环形缓冲 4096B */
    char line[512];
    char resp[2048];
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
            int n = 0;
            if (strcmp(line, "SNAP") == 0) {
                hal_display_snapshot_arm();
            } else if (strcmp(line, "NEXT") == 0) {
                swipe_nav_step(1); /* 循环切下一个已安装应用（触摸失效时的兜底） */
            } else if (strncmp(line, "RAD ", 4) == 0) {
                int r = atoi(line + 4);
                if (r >= 1 && r <= 240) {
                    app_radius_request_radius(r); /* gui_task 的 on_running 消费 */
                    n = snprintf(resp, sizeof(resp), "#RAD %d\n", r);
                } else {
                    n = snprintf(resp, sizeof(resp), "#ERR radius 1-240\n");
                }
            } else if (strncmp(line, "PAGE ", 5) == 0) {
                int p = atoi(line + 5);
                if (p >= 0 && p <= 2) {
                    app_radius_request_page(p);
                    n = snprintf(resp, sizeof(resp), "#PAGE %d\n", p);
                } else {
                    n = snprintf(resp, sizeof(resp), "#ERR page 0-2\n");
                }
            } else if (strcmp(line, "REBOOT") == 0) {
                esp_restart();
            } else if (strcmp(line, "PING") == 0) {
                n = snprintf(resp, sizeof(resp), "#HELLO {\"name\":\"CubeBox\",\"fw\":\"%s\",\"proto\":2}\n",
                             esp_app_get_description()->version);
            } else if (strcmp(line, "GETCFG") == 0) {
                n = snprintf(resp, sizeof(resp), "#CFG %s\n",
                             config_store_get_json(line, sizeof(line)));
            } else if (strncmp(line, "SETCFG ", 7) == 0) {
                char err[96];
                if (config_store_set_json(line + 7, err, sizeof(err))) {
                    n = snprintf(resp, sizeof(resp), "#CFG %s\n",
                                 config_store_get_json(line, sizeof(line)));
                } else {
                    n = snprintf(resp, sizeof(resp), "#ERR %s\n", err);
                }
            } else if (strcmp(line, "SCAN") == 0) {
                /* 阻塞到扫描完成（~2-4s）：网页端请求串行化，后续命令排队，心跳不受影响 */
                n = snprintf(resp, sizeof(resp), "#NETWORKS ");
                if (hal_wifi_scan(resp + n, sizeof(resp) - n - 1)) {
                    n += (int)strlen(resp + n);
                    resp[n++] = '\n';
                    resp[n] = '\0';
                } else {
                    n = snprintf(resp, sizeof(resp), "#ERR wifi busy\n");
                }
            } else if (strcmp(line, "TIME") == 0) {
                time_t now = time(NULL);
                struct tm local;
                localtime_r(&now, &local);
                n = snprintf(resp, sizeof(resp),
                             "#TIME {\"synced\":%s,\"utc\":%lld,\"local\":\"%04d-%02d-%02d %02d:%02d:%02d\"}\n",
                             time_sync_is_synced() ? "true" : "false", (long long)now,
                             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                             local.tm_hour, local.tm_min, local.tm_sec);
            }
            if (n > 0) {
                usb_serial_jtag_write_bytes(resp, n, pdMS_TO_TICKS(1000));
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
 *   2. config_store_apply_pending：消费配置副作用（亮度/tz/Wi-Fi/NTP，console 置位）
 *   3. apps_update：消费跨任务的应用切换请求 + 调当前应用 on_running
 *   4. lv_timer_handler 驱动 LVGL 刷新
 */
static void gui_task(void *arg)
{
    (void)arg;
    while (1) {
        if (hal_display_snapshot_begin_if_requested()) {
            lv_obj_invalidate(lv_scr_act());
        }

        config_store_apply_pending();

        apps_update();

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

    /* HAL：先电源后显示（显示复位依赖 ALDO3），再触摸/RTC（失败不致命） */
    hal_power_init(i2c_bus);
    hal_display_init();
    hal_touch_init(i2c_bus);
    hal_rtc_init(i2c_bus);

    /* 配置：NVS 读取 + 置 pending，gui_task 首轮应用（亮度/tz/Wi-Fi/NTP，含开机默认值）。
     * 必须先于 hal_wifi_init：esp_wifi 内部依赖 NVS（osi_nvs） */
    config_store_init();

    /* 网络 + 时间：STA 启动（不连接，凭据经 apply_pending 幂等应用）、SNTP 回调注册、
     * RTC 恢复系统时间（电池走时，重启不丢） */
    hal_wifi_init();
    time_sync_init();

    /* 安装应用（安装顺序即左右滑动的切换顺序），默认启动点阵时钟 */
    if (!apps_install(&app_clock)) {
        ESP_LOGE(TAG, "apps_install(app_clock) failed (table full or duplicate)");
    }
    if (!apps_install(&app_test)) {
        ESP_LOGE(TAG, "apps_install(app_test) failed (table full or duplicate)");
    }
    if (!apps_install(&app_radius)) { /* 圆角测量研究工具（docs/apps/app_radius.md） */
        ESP_LOGE(TAG, "apps_install(app_radius) failed (table full or duplicate)");
    }
    apps_request_open(app_clock.name);

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
    xTaskCreate(console_task, "console", 8192, NULL, 5, NULL); /* 行/响应缓冲 ~2.6KB（#NETWORKS） */
}
