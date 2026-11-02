#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include "lvgl.h"
#include "bsp_power.h"
#include "bsp_rtc.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include "ui_keyboard.h"
#include "app_wifi.h"

LV_FONT_DECLARE(lv_font_montserrat_48);

#define TAG "main"

/* 低功耗 idle：60s 无触摸进入，触摸唤醒（只恢复亮度，不启动任何无线电） */
#define IDLE_TIMEOUT_MS    60000
static bool s_low_power = false;
static uint32_t s_low_power_enter_ms = 0;

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0

/*
 * 时间同步任务：通过 USB-Serial/JTAG（辅助 console）接收
 * "SETTIME YYYY-MM-DD HH:MM:SS" 命令，把电脑本地时间写入 PCF85063 RTC。
 */

/* 键盘调试命令（console 任务置标志，gui_task 消费——LVGL 只在 gui 上下文调用） */
typedef enum {
    KBD_CMD_NONE = 0,
    KBD_CMD_OPEN_TEST,
    KBD_CMD_DUMP,
} kbd_dbg_cmd_t;

static volatile kbd_dbg_cmd_t s_kbd_dbg_cmd = KBD_CMD_NONE;
static volatile int s_kbd_dbg_key = -1;
static volatile bool s_uitest_req = false;

static void kbd_test_ok(const char *password, void *user_data)
{
    ESP_LOGI(TAG, "KBD OK: password='%s'", password);
    (void)user_data;
}

static void kbd_test_cancel(void *user_data)
{
    ESP_LOGI(TAG, "KBD CANCEL");
    (void)user_data;
}

static void gui_handle_kbd_debug(void)
{
    if (s_kbd_dbg_key >= 0) {
        ui_keyboard_debug_tap(s_kbd_dbg_key);
        s_kbd_dbg_key = -1;
    }
    kbd_dbg_cmd_t cmd = s_kbd_dbg_cmd;
    s_kbd_dbg_cmd = KBD_CMD_NONE;
    if (cmd == KBD_CMD_OPEN_TEST) {
        const ui_keyboard_cfg_t cfg = {
            .title = "TEST-NET",
            .on_ok = kbd_test_ok,
            .on_cancel = kbd_test_cancel,
        };
        ui_settings_show();
        ui_keyboard_open(lv_scr_act(), &cfg);
    } else if (cmd == KBD_CMD_DUMP) {
        char st[128];
        ui_keyboard_debug_state(st, sizeof(st));
        ESP_LOGI(TAG, "%s", st);
    }
}

/* 电池放电日志：RAM 环形缓冲自记录（拔线放电时 USB 断开，插回后 BATTLOG DUMP 导出）。
 * 重启即失；采样固定 60s 一条，1024 条 ≈ 17 小时 */
#define BATTLOG_INTERVAL_S   60
#define BATTLOG_MAX_ENTRIES  1024

typedef struct {
    uint32_t uptime_s;
    uint8_t pct;
    uint16_t vbat_mv;
    uint8_t flags;   /* bit0=低功耗 bit1=VBUS在位 bit2=充电中 */
} battlog_entry_t;

static battlog_entry_t s_battlog[BATTLOG_MAX_ENTRIES];
static volatile uint16_t s_battlog_head = 0;   /* 下一条写入位置 */
static volatile uint16_t s_battlog_count = 0;

static void batt_log_task(void *arg)
{
    (void)arg;
    while (1) {
        bsp_power_batt_info_t b;
        if (bsp_power_get_battery_info(&b) == ESP_OK) {
            uint16_t idx = s_battlog_head;
            s_battlog[idx].uptime_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
            s_battlog[idx].pct = b.pct;
            s_battlog[idx].vbat_mv = b.vbat_mv;
            s_battlog[idx].flags = (s_low_power ? 1U : 0U) |
                                   (b.vbus_present ? 2U : 0U) |
                                   (b.charging ? 4U : 0U);
            s_battlog_head = (uint16_t)((idx + 1) % BATTLOG_MAX_ENTRIES);
            if (s_battlog_count < BATTLOG_MAX_ENTRIES) {
                s_battlog_count++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BATTLOG_INTERVAL_S * 1000));
    }
}

/* 无头复现用户流程：设置页 → WiFi 页扫描 → 点行 0（应弹键盘），逐步打点定位死锁 */
static void gui_handle_uitest(void)
{
    if (!s_uitest_req) {
        return;
    }
    s_uitest_req = false;

    ESP_LOGI(TAG, "UITEST: 1/3 settings show");
    ui_settings_show();
    vTaskDelay(pdMS_TO_TICKS(600));

    ESP_LOGI(TAG, "UITEST: 2/3 wifi show + scan");
    ui_wifi_show();
    vTaskDelay(pdMS_TO_TICKS(4000));

    ESP_LOGI(TAG, "UITEST: 3/3 tap row 0");
    ui_wifi_debug_tap_row(0);
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "UITEST: done (clock should continue)");
}

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
            } else if (strcmp(line, "REBOOT") == 0) {
                esp_restart();
            } else if (strcmp(line, "PAGE SETTINGS") == 0) {
                ui_nav_request(UI_NAV_SETTINGS);
            } else if (strcmp(line, "PAGE WIFI") == 0) {
                ui_nav_request(UI_NAV_WIFI);
            } else if (strcmp(line, "PAGE CLOCK") == 0) {
                ui_nav_request(UI_NAV_CLOCK);
            } else if (strcmp(line, "WSCAN") == 0) {
                app_wifi_ensure_started();
                app_wifi_cmd_scan();
            } else if (strncmp(line, "WCONNECT ", 9) == 0) {
                /* WCONNECT <ssid> [password]：无第三段=用保存凭据；"-"=开放网络 */
                char ssid[33] = {0};
                char pwd[65] = {0};
                int n = sscanf(line + 9, "%32s %64s", ssid, pwd);
                if (n >= 1) {
                    app_wifi_ensure_started();
                    if (n == 1) {
                        app_wifi_cmd_connect(ssid, NULL);
                    } else {
                        app_wifi_cmd_connect(ssid, strcmp(pwd, "-") == 0 ? "" : pwd);
                    }
                }
            } else if (strcmp(line, "KBD") == 0) {
                s_kbd_dbg_cmd = KBD_CMD_OPEN_TEST;
            } else if (strncmp(line, "KBDMOCK ", 8) == 0) {
                int k = -1;
                if (sscanf(line + 8, "%d", &k) == 1) {
                    s_kbd_dbg_key = k;
                }
            } else if (strcmp(line, "KBDDUMP") == 0) {
                s_kbd_dbg_cmd = KBD_CMD_DUMP;
            } else if (strncmp(line, "BRIGHT ", 7) == 0) {
                /* BRIGHT <1-100>：直接设背光（标定低功耗可见阈值用，不落 NVS） */
                int v = 0;
                if (sscanf(line + 7, "%d", &v) == 1 && v >= 1 && v <= 100) {
                    bsp_lcd_set_backlight((uint8_t)v);
                    ESP_LOGI(TAG, "backlight set to %d%%", v);
                }
            } else if (strcmp(line, "BATT") == 0) {
                /* BATT：即时读电池状态（电量/电压/供电/充电），寄存器见 bsp_power.h */
                bsp_power_batt_info_t b;
                if (bsp_power_get_battery_info(&b) == ESP_OK) {
                    ESP_LOGI(TAG, "BATT pct=%u vbat=%umV vbus=%d chg=%d st=0x%02X lp=%d",
                             b.pct, b.vbat_mv, b.vbus_present, b.charging, b.raw_status,
                             s_low_power);
                } else {
                    ESP_LOGE(TAG, "BATT read failed");
                }
            } else if (strcmp(line, "BATTLOG DUMP") == 0) {
                /* 导出 RAM 放电日志；行格式 "BL <uptime_s> <pct> <mV> <flags>"。
                 * 注意：仅放电段（flags bit1=0）参与斜率计算，插回 USB 会开始充电 */
                uint16_t n = s_battlog_count;
                uint16_t start = (n == BATTLOG_MAX_ENTRIES) ? s_battlog_head : 0;
                ESP_LOGI(TAG, "BL HEAD n=%u", n);
                for (uint16_t i = 0; i < n; i++) {
                    const battlog_entry_t *e = &s_battlog[(start + i) % BATTLOG_MAX_ENTRIES];
                    ESP_LOGI(TAG, "BL %u %u %u %u",
                             (unsigned)e->uptime_s, (unsigned)e->pct,
                             (unsigned)e->vbat_mv, (unsigned)e->flags);
                }
                ESP_LOGI(TAG, "BL END n=%u", n);
            } else if (strcmp(line, "BATTLOG CLEAR") == 0) {
                s_battlog_head = 0;
                s_battlog_count = 0;
                ESP_LOGI(TAG, "BL cleared");
            } else if (strcmp(line, "TPTEST") == 0) {
                bsp_touch_debug_poll(10);
            } else if (strcmp(line, "UITEST") == 0) {
                s_uitest_req = true;
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
    /* 创建常驻页面：时钟页（设为活动页）+ 设置页 + WiFi 页 */
    ui_clock_init();
    ui_settings_create();
    ui_wifi_create();

    uint32_t last_rtc_update = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Update RTC time and date every 1000ms */
        if (now - last_rtc_update >= 1000 || last_rtc_update == 0) {
            ui_clock_tick_refresh();
            last_rtc_update = now;
        }

        /* 低功耗 idle 管理：每个循环检查（10ms 粒度，唤醒响应及时） */
        uint32_t last_act = bsp_touch_last_activity_ms();
        if (!s_low_power) {
            if (now - last_act >= IDLE_TIMEOUT_MS) {
                s_low_power = true;
                s_low_power_enter_ms = now;
                bsp_lcd_set_brightness_raw(BSP_LCD_BRIGHTNESS_LOW_PCT);
                bsp_touch_set_swallow_once();
                ESP_LOGI(TAG, "idle: enter low power (brightness %d%%)", BSP_LCD_BRIGHTNESS_LOW_PCT);
            }
        } else if ((int32_t)(last_act - s_low_power_enter_ms) > 0) {
            /* 进入低功耗之后有触摸 → 唤醒，只恢复亮度（首次触摸已被吞掉防误触） */
            s_low_power = false;
            bsp_lcd_restore_user_brightness();
            ESP_LOGI(TAG, "idle: wake, restore brightness %u%%", bsp_lcd_get_brightness_pct());
        }

        /* 页面导航请求（控制台/手势路径之外的远程触发） */
        ui_nav_req_t nav = ui_nav_poll_request();
        if (nav == UI_NAV_SETTINGS) {
            ui_settings_show();
        } else if (nav == UI_NAV_WIFI) {
            ui_wifi_show();
        } else if (nav == UI_NAV_CLOCK) {
            ui_clock_show();
        }

        /* 键盘调试命令（KBD/KBDMOCK/KBDDUMP） */
        gui_handle_kbd_debug();

        /* 无头复现（UITEST） */
        gui_handle_uitest();

        /* 滑动事件（检测在 indev 回调内置位；必须在 LVGL 输入处理外执行切屏） */
        lv_dir_t swipe = bsp_touch_consume_swipe();
        if (swipe != 0) {
            ui_nav_swipe_dispatch(swipe);
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

    /* NVS：亮度设置 + WiFi 凭据存储（不含无线电，开机即初始化） */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

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
    bsp_touch_init(i2c_bus); /* CST9220 触摸 + LVGL pointer indev */

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
    xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 5, NULL);
    xTaskCreate(batt_log_task, "batt_log", 3072, NULL, 4, NULL); /* 电池放电自记录 */
}
