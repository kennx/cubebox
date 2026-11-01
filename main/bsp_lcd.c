#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/spi_master.h>
#include <driver/usb_serial_jtag.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "esp_lcd_sh8601.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "bsp_lcd.h"
#include "bsp_power.h"
#include "ui_clock.h"

#define TAG "bsp_lcd"

/* LCD QSPI */
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_PCLK_GPIO   0
#define LCD_D0_GPIO     1
#define LCD_D1_GPIO     2
#define LCD_D2_GPIO     3
#define LCD_D3_GPIO     4
#define LCD_CS_GPIO     15
#define LCD_WIDTH       480
#define LCD_HEIGHT      480

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;

static bsp_lcd_flush_mode_t s_flush_mode = BSP_LCD_FLUSH_CLOCK_OVERLAY;

/* 用户亮度（NVS 持久化）；低功耗 dim 不修改该值 */
static uint8_t s_brightness_pct = 100;

void bsp_lcd_set_brightness_live(uint8_t pct)
{
    if (pct < 10) {
        pct = 10;
    }
    if (pct > 100) {
        pct = 100;
    }
    s_brightness_pct = pct;
    bsp_lcd_set_backlight(pct);
}

void bsp_lcd_set_brightness_pct(uint8_t pct)
{
    bsp_lcd_set_brightness_live(pct);

    nvs_handle_t nvs;
    if (nvs_open("settings", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "brightness", pct);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

uint8_t bsp_lcd_get_brightness_pct(void)
{
    return s_brightness_pct;
}

void bsp_lcd_set_brightness_raw(uint8_t pct)
{
    bsp_lcd_set_backlight(pct);
}

void bsp_lcd_restore_user_brightness(void)
{
    bsp_lcd_set_backlight(s_brightness_pct);
}

void bsp_lcd_set_flush_mode(bsp_lcd_flush_mode_t mode)
{
    s_flush_mode = mode;
}

bsp_lcd_flush_mode_t bsp_lcd_get_flush_mode(void)
{
    return s_flush_mode;
}

/* 截图调试：请求标志任意任务置位，活动状态仅 gui_task 上下文访问 */
static volatile bool s_snap_req = false;
static bool s_snap_active = false;
static char s_snap_line[LCD_WIDTH * 4 + 32];

void bsp_lcd_snapshot_arm(void)
{
    s_snap_req = true;
}

bool bsp_lcd_snapshot_begin_if_requested(void)
{
    if (s_snap_req && !s_snap_active) {
        s_snap_req = false;
        s_snap_active = true;
        return true;
    }
    return false;
}

/* LCD init commands - must be global/static for SH8601 driver */
static const uint8_t lcd_data_00[] = {0x00};
static const uint8_t lcd_data_20[] = {0x20};
static const uint8_t lcd_data_10[] = {0x10};
static const uint8_t lcd_data_A0[] = {0xA0};
static const uint8_t lcd_data_80[] = {0x80};
static const uint8_t lcd_data_55[] = {0x55};
static const uint8_t lcd_data_30[] = {0x30};
static const uint8_t lcd_data_FF[] = {0xFF};
static const uint8_t lcd_data_2A[] = {0x00, 0x00, 0x01, 0xDF};
static const uint8_t lcd_data_2B[] = {0x00, 0x00, 0x01, 0xDF};

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, lcd_data_00, 0, 600},
    {0xFE, lcd_data_20, 1, 0},
    {0x19, lcd_data_10, 1, 0},
    {0x1C, lcd_data_A0, 1, 0},
    {0xFE, lcd_data_00, 1, 0},
    {0xC4, lcd_data_80, 1, 0},
    {0x3A, lcd_data_55, 1, 0},
    {0x35, lcd_data_00, 1, 0},
    {0x36, lcd_data_30, 1, 0},
    {0x53, lcd_data_20, 1, 0},
    {0x51, lcd_data_FF, 1, 0},
    {0x63, lcd_data_FF, 1, 0},
    {0x2A, lcd_data_2A, 4, 0},
    {0x2B, lcd_data_2B, 4, 0},
    {0x29, lcd_data_00, 0, 100},
};

void bsp_lcd_set_backlight(uint8_t brightness)
{
    uint8_t bl_val = (brightness * 255) / 100;
    uint32_t cmd = (0x02 << 24) | (0x51 << 8);
    ESP_LOGD(TAG, "Setting backlight: brightness=%d, bl_val=0x%02X", brightness, bl_val);
    esp_err_t ret = esp_lcd_panel_io_tx_param(lcd_io, cmd, &bl_val, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight command failed: %s", esp_err_to_name(ret));
    }
}

static void lcd_diagnostic_fill(uint16_t color)
{
    static uint16_t line_buf[LCD_WIDTH * 20];
    for (size_t i = 0; i < LCD_WIDTH * 20; i++) {
        line_buf[i] = color;
    }

    for (int y = 0; y < LCD_HEIGHT; y += 20) {
        esp_err_t ret = esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, LCD_WIDTH, y + 20, line_buf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Diagnostic fill failed at y=%d: %s", y, esp_err_to_name(ret));
            return;
        }
    }
    ESP_LOGI(TAG, "Diagnostic fill color=0x%04X completed", color);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    static uint32_t flush_count = 0;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    uint32_t width = offsetx2 - offsetx1 + 1;
    uint32_t height = offsety2 - offsety1 + 1;
    uint32_t size = width * height;

    /* A static swap buffer with safe margin to isolate the big-endian pixel stream and prevent rounder-induced underflow */
    static lv_color_t swap_buf[LCD_WIDTH * 60];
    if (size > LCD_WIDTH * 60) {
        ESP_LOGE(TAG, "Flush size too large: %lu, max: %d", size, LCD_WIDTH * 60);
        lv_disp_flush_ready(drv);
        return;
    }

    /* 点阵时钟像素叠绘：在送显前对 swap_buf 做逐像素覆盖（仅时钟页模式）；
     * 设置页模式纯透传，但仍要经 swap_buf 隔离（draw_bitmap 异步读取期间
     * LVGL 可能复用其绘制缓冲） */
    if (s_flush_mode == BSP_LCD_FLUSH_CLOCK_OVERLAY) {
        ui_clock_overlay_apply(area, color_map, swap_buf);
    } else {
        memcpy(swap_buf, color_map, size * sizeof(lv_color_t));
    }

    /* 截图调试：逐行流出最终送显像素（RGB565 hex，内存字节序）。
     * 每行一条 "#SNAP x1 y x2 y" + 该行 hex（≤1952B 单次写，防溢出），
     * host 端按坐标覆盖重组；END 后仍可能收到个别迟到条带，host 端忽略即可。 */
    if (s_snap_active) {
        static const char hx[] = "0123456789abcdef";
        for (uint32_t row = 0; row < height; row++) {
            int gy = offsety1 + row;
            int n = snprintf(s_snap_line, sizeof(s_snap_line), "#SNAP %d %d %d %d\n",
                             offsetx1, gy, offsetx2, gy);
            for (uint32_t x = 0; x < width; x++) {
                uint16_t v = swap_buf[row * width + x].full;
                s_snap_line[n++] = hx[(v >> 4) & 0xF];
                s_snap_line[n++] = hx[v & 0xF];
                s_snap_line[n++] = hx[(v >> 12) & 0xF];
                s_snap_line[n++] = hx[(v >> 8) & 0xF];
            }
            s_snap_line[n++] = '\n';
            usb_serial_jtag_write_bytes(s_snap_line, n, pdMS_TO_TICKS(1000));
        }
        if (offsety2 == LCD_HEIGHT - 1) {
            usb_serial_jtag_write_bytes("#SNAP END\n", 10, pdMS_TO_TICKS(1000));
            s_snap_active = false;
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, swap_buf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL flush failed area=(%d,%d)-(%d,%d): %s",
                 offsetx1, offsety1, offsetx2, offsety2, esp_err_to_name(ret));
        lv_disp_flush_ready(drv);
        return;
    }
    flush_count++;
    if (flush_count <= 5 || flush_count % 60 == 0) {
        ESP_LOGI(TAG, "LVGL flush #%lu area=(%d,%d)-(%d,%d) size=%lu",
                 flush_count, offsetx1, offsety1, offsetx2, offsety2, size);
    }
}

static void lvgl_tick_task(void *arg)
{
    lv_tick_inc(2);
}

static void lvgl_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    /* Round down start coordinates to nearest even number */
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;

    /* Round up end coordinates to nearest odd number to guarantee even width/height */
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static lv_disp_drv_t disp_drv; // Global static to make it alive permanently

void bsp_lcd_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PCLK_GPIO,
        .data0_io_num = LCD_D0_GPIO,
        .data1_io_num = LCD_D1_GPIO,
        .data2_io_num = LCD_D2_GPIO,
        .data3_io_num = LCD_D3_GPIO,
        .max_transfer_sz = LCD_WIDTH * 50 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &lcd_io));

    sh8601_vendor_config_t vendor_config = {0};
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    ESP_LOGI(TAG, "Creating SH8601 panel...");
    esp_err_t ret = esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel);
    ESP_LOGI(TAG, "esp_lcd_new_panel_sh8601 result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Performing LCD power reset...");
    bsp_power_lcd_reset();

    ESP_LOGI(TAG, "Initializing LCD panel...");
    ret = esp_lcd_panel_init(lcd_panel);
    ESP_LOGI(TAG, "esp_lcd_panel_init result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Turning LCD display on...");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    /* 应用 NVS 保存的用户亮度（首次启动默认 100） */
    {
        nvs_handle_t nvs;
        if (nvs_open("settings", NVS_READONLY, &nvs) == ESP_OK) {
            uint8_t saved = 100;
            if (nvs_get_u8(nvs, "brightness", &saved) == ESP_OK) {
                if (saved >= 10 && saved <= 100) {
                    s_brightness_pct = saved;
                }
            }
            nvs_close(nvs);
        }
    }
    bsp_lcd_set_backlight(s_brightness_pct);
    lcd_diagnostic_fill(0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_diagnostic_fill(0x0000);

    ESP_LOGI(TAG, "LCD initialized");

    /* Initialize LVGL */
    lv_init();

    /* Allocate buffer for LVGL, using internal DMA capable SRAM */
    lv_color_t *buf1 = heap_caps_malloc(LCD_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 != NULL);

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_WIDTH * 40);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = lcd_panel;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    lv_disp_drv_register(&disp_drv);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io, &cbs, &disp_drv));

    /* Set up 2ms high-resolution timer for LVGL tick */
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2000)); // 2000 us = 2ms

    ESP_LOGI(TAG, "LVGL engine and display driver registered");
}
