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
#include "hal/hal_display.h"
#include "hal/hal_power.h"

#define TAG "hal_display"

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

/* flush 缓冲约束链：LVGL 绘制缓冲 DRAW_BUF_LINES 行 → rounder 把 flush 区域
 * 两侧各扩 1 像素，单次 flush 上限 FLUSH_MAX_LINES 行 → swap_buf 容量与 SPI
 * DMA max_transfer_sz 都必须覆盖该上限（RGB565 每行 2 字节） */
#define LCD_DRAW_BUF_LINES    40
#define LCD_FLUSH_MAX_LINES   (LCD_DRAW_BUF_LINES + 2)

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;

/* 截图调试：请求标志任意任务置位，活动状态仅 gui_task 上下文访问 */
static volatile bool s_snap_req = false;
static bool s_snap_active = false;
static char s_snap_line[LCD_WIDTH * 4 + 32];

void hal_display_snapshot_arm(void)
{
    s_snap_req = true;
}

bool hal_display_snapshot_begin_if_requested(void)
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

void hal_display_set_backlight(uint8_t brightness)
{
    uint8_t bl_val = (brightness * 255) / 100;
    uint32_t cmd = (0x02 << 24) | (0x51 << 8);
    esp_err_t ret = esp_lcd_panel_io_tx_param(s_lcd_io, cmd, &bl_val, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight command failed: %s", esp_err_to_name(ret));
    }
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
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    uint32_t width = offsetx2 - offsetx1 + 1;
    uint32_t height = offsety2 - offsety1 + 1;
    uint32_t size = width * height;

    /* A static swap buffer to isolate the big-endian pixel stream and prevent
     * rounder-induced underflow. draw_bitmap 读取是异步的，期间 LVGL 可能复用
     * 其绘制缓冲，因此必须先拷贝再送显 */
    static lv_color_t swap_buf[LCD_WIDTH * LCD_FLUSH_MAX_LINES];
    if (size > LCD_WIDTH * LCD_FLUSH_MAX_LINES) {
        ESP_LOGE(TAG, "Flush size too large: %lu, max: %d", size, LCD_WIDTH * LCD_FLUSH_MAX_LINES);
        lv_disp_flush_ready(drv);
        return;
    }
    memcpy(swap_buf, color_map, size * sizeof(lv_color_t));

    /* 截图调试：逐行流出最终送显像素（RGB565 hex，低字节在前的内存序）。
     * 每行一条 "#SNAP x1 y x2 y" + 该行 hex（≤1952B 单次写，防溢出），
     * host 端按坐标覆盖重组；END 后仍可能收到个别迟到条带，host 端忽略即可。
     * LV_COLOR_16_SWAP=y 时 .full 的位模式 = bswap16(逻辑值)（位域 green 被拆
     * 到两端），流出前交换回逻辑值，host 端解析格式保持不变。 */
    if (s_snap_active) {
        static const char hx[] = "0123456789abcdef";
        for (uint32_t row = 0; row < height; row++) {
            int gy = offsety1 + row;
            int n = snprintf(s_snap_line, sizeof(s_snap_line), "#SNAP %d %d %d %d\n",
                             offsetx1, gy, offsetx2, gy);
            for (uint32_t x = 0; x < width; x++) {
                uint16_t v = swap_buf[row * width + x].full;
                v = (uint16_t)((v << 8) | (v >> 8));
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

void hal_display_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PCLK_GPIO,
        .data0_io_num = LCD_D0_GPIO,
        .data1_io_num = LCD_D1_GPIO,
        .data2_io_num = LCD_D2_GPIO,
        .data3_io_num = LCD_D3_GPIO,
        .max_transfer_sz = LCD_WIDTH * LCD_FLUSH_MAX_LINES * 2,
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
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &s_lcd_io));

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
    esp_err_t ret = esp_lcd_new_panel_sh8601(s_lcd_io, &panel_config, &s_lcd_panel);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Performing LCD power reset...");
    hal_power_lcd_reset();

    ESP_LOGI(TAG, "Initializing LCD panel...");
    ret = esp_lcd_panel_init(s_lcd_panel);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Turning LCD display on...");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel, true));

    hal_display_set_backlight(100);

    ESP_LOGI(TAG, "LCD initialized");

    /* Initialize LVGL */
    lv_init();

    /* Allocate buffer for LVGL, using internal DMA capable SRAM */
    lv_color_t *buf1 = heap_caps_malloc(LCD_WIDTH * LCD_DRAW_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 != NULL);

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_WIDTH * LCD_DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = s_lcd_panel;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    lv_disp_drv_register(&disp_drv);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_lcd_io, &cbs, &disp_drv));

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
