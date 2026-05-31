#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "esp_lcd_sh8601.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <math.h>

LV_FONT_DECLARE(lv_font_montserrat_48);

#define DOT_SIZE      6
#define DOT_GAP       2
#define CHAR_GAP      10
#define RING_RADIUS   225
#define RING_DOTS     60

// 5x7 点阵字模: 0-9 以及 冒号(:)
static const uint8_t font_5x7[11][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0: 01110
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1: 00100
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x01, 0x02, 0x0E, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}, // 9
    {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}  // : (冒号)
};

#define TAG "main"

/* I2C */
#define I2C_SDA_GPIO    8
#define I2C_SCL_GPIO    7
#define I2C_PORT        I2C_NUM_0
#define AXP2101_ADDR    0x34
#define PCF85063_ADDR   0x51  /* RTC I2C address */

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

/* AXP2101 registers */
#define AXP2101_LDO_ONOFF_CTRL0     0x90
#define AXP2101_DC_ONOFF_DVM_CTRL   0x80
#define AXP2101_DC_VOL0_CTRL        0x82
#define AXP2101_LDO_VOL0_CTRL       0x92
#define AXP2101_LDO_VOL1_CTRL       0x93
#define AXP2101_LDO_VOL2_CTRL       0x94
#define AXP2101_LDO_VOL3_CTRL       0x95

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t axp2101_dev = NULL;
static i2c_master_dev_handle_t pcf85063_dev = NULL;  /* RTC device handle */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;

/* LCD init commands - must be global/static for SH8601 driver */
static const uint8_t lcd_data_00[] = {0x00};
static const uint8_t lcd_data_20[] = {0x20};
static const uint8_t lcd_data_2C[] = {0x2C};
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
    {0x53, lcd_data_2C, 1, 0},
    {0x51, lcd_data_FF, 1, 0},
    {0x63, lcd_data_FF, 1, 0},
    {0x2A, lcd_data_2A, 4, 0},
    {0x2B, lcd_data_2B, 4, 0},
    {0x29, lcd_data_00, 0, 100},
};

static esp_err_t axp2101_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(axp2101_dev, buf, 2, pdMS_TO_TICKS(1000));
}

static esp_err_t axp2101_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(axp2101_dev, &reg, 1, val, 1, pdMS_TO_TICKS(1000));
}

static void axp2101_set_bit(uint8_t reg, uint8_t bit)
{
    uint8_t val;
    if (axp2101_read_reg(reg, &val) == ESP_OK) {
        val |= (1 << bit);
        axp2101_write_reg(reg, val);
    }
}

static void axp2101_clr_bit(uint8_t reg, uint8_t bit)
{
    uint8_t val;
    if (axp2101_read_reg(reg, &val) == ESP_OK) {
        val &= ~(1 << bit);
        axp2101_write_reg(reg, val);
    }
}

static inline uint8_t bcd2dec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

static void rtc_pcf85063_init(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &pcf85063_dev));
    ESP_LOGI(TAG, "PCF85063 RTC device added to I2C bus");
}

static esp_err_t rtc_pcf85063_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_addr = 0x04; /* Seconds register address */
    uint8_t data[3] = {0};
    
    /* Read 3 bytes starting from 0x04: Seconds, Minutes, Hours */
    esp_err_t ret = i2c_master_transmit_receive(pcf85063_dev, &reg_addr, 1, data, 3, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        return ret;
    }
    
    *second = bcd2dec(data[0] & 0x7F);
    *minute = bcd2dec(data[1] & 0x7F);
    *hour   = bcd2dec(data[2] & 0x3F); /* 24H mode max 23 */
    
    return ESP_OK;
}

static void pmic_init(void)
{
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
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &axp2101_dev));

    /* DC1 = 3300mV, value = (3300 - 1500) / 100 = 18 */
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_DC_VOL0_CTRL, 18));
    /* ALDO1~4 = 3300mV, value = (3300 - 500) / 100 = 28 */
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL0_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL1_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL2_CTRL, 28));
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_VOL3_CTRL, 28));

    /* Enable DC1 (bit 0) and ALDO1,2,4 (bit 0,1,3). Do NOT enable ALDO3 here - LCD init controls it */
    axp2101_set_bit(AXP2101_DC_ONOFF_DVM_CTRL, 0);
    uint8_t ldo_onoff;
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &ldo_onoff);
    ldo_onoff |= 0x0B;  /* bits 0,1,3 = ALDO1, ALDO2, ALDO4 */
    axp2101_write_reg(AXP2101_LDO_ONOFF_CTRL0, ldo_onoff);

    /* Verify registers */
    uint8_t dc_onoff, dc_vol, ldo_vol0, ldo_vol3;
    axp2101_read_reg(AXP2101_DC_ONOFF_DVM_CTRL, &dc_onoff);
    axp2101_read_reg(AXP2101_DC_VOL0_CTRL, &dc_vol);
    axp2101_read_reg(AXP2101_LDO_VOL0_CTRL, &ldo_vol0);
    axp2101_read_reg(AXP2101_LDO_VOL3_CTRL, &ldo_vol3);
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &ldo_onoff);
    ESP_LOGI(TAG, "PMIC verify: DC_ONOFF=0x%02X DC_VOL=0x%02X LDO0_VOL=0x%02X LDO3_VOL=0x%02X LDO_ONOFF=0x%02X",
             dc_onoff, dc_vol, ldo_vol0, ldo_vol3, ldo_onoff);

    ESP_LOGI(TAG, "PMIC initialized");
}

static esp_err_t lcd_power_reset(void)
{
    uint8_t val;
    /* High-Low-High reset sequence (match official Waveshare code) */
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &val);
    ESP_LOGI(TAG, "ALDO3 before reset: 0x%02X (bit2=%d)", val, (val >> 2) & 1);
    
    axp2101_set_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Power on */
    vTaskDelay(pdMS_TO_TICKS(100));
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &val);
    ESP_LOGI(TAG, "ALDO3 after power on: 0x%02X (bit2=%d)", val, (val >> 2) & 1);
    
    axp2101_clr_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Reset */
    vTaskDelay(pdMS_TO_TICKS(100));
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &val);
    ESP_LOGI(TAG, "ALDO3 after reset: 0x%02X (bit2=%d)", val, (val >> 2) & 1);
    
    axp2101_set_bit(AXP2101_LDO_ONOFF_CTRL0, 2);  /* Release reset */
    vTaskDelay(pdMS_TO_TICKS(100));
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &val);
    ESP_LOGI(TAG, "ALDO3 after release: 0x%02X (bit2=%d)", val, (val >> 2) & 1);
    
    return ESP_OK;
}

static void lcd_set_backlight(uint8_t brightness)
{
    uint8_t bl_val = (brightness * 255) / 100;
    uint32_t cmd = (0x02 << 24) | (0x51 << 8);
    ESP_LOGI(TAG, "Setting backlight: brightness=%d, bl_val=0x%02X, cmd=0x%08X", brightness, bl_val, cmd);
    esp_err_t ret = esp_lcd_panel_io_tx_param(lcd_io, cmd, &bl_val, 1);
    ESP_LOGI(TAG, "Backlight command result: %s", esp_err_to_name(ret));
}static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
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

    /* A static swap buffer to isolate the big-endian pixel stream from LVGL's own drawing buffer */
    static lv_color_t swap_buf[LCD_WIDTH * 40];
    if (size > LCD_WIDTH * 40) {
        size = LCD_WIDTH * 40;
    }

    for (uint32_t i = 0; i < size; i++) {
        swap_buf[i].full = (color_map[i].full >> 8) | (color_map[i].full << 8);
    }

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, swap_buf);
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

static void lcd_init(void)
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
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = true,
        },
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
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
    ESP_ERROR_CHECK(lcd_power_reset());
    
    ESP_LOGI(TAG, "Initializing LCD panel...");
    ret = esp_lcd_panel_init(lcd_panel);
    ESP_LOGI(TAG, "esp_lcd_panel_init result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    lcd_set_backlight(100);

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

static void gui_task(void *arg)
{
    static lv_obj_t *dot_objs[8][5][7];
    static lv_obj_t *ring_dots[RING_DOTS];
    /* Set screen background to black */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 创建点阵容器并使其水平居中 */
    lv_obj_t *time_container = lv_obj_create(scr);
    lv_obj_set_size(time_container, 374, 54);
    /* 移除容器背景、边框、内边距，使其完全透明 */
    lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_container, 0, 0);
    lv_obj_set_style_pad_all(time_container, 0, 0);
    lv_obj_align(time_container, LV_ALIGN_CENTER, 0, 0); // 完美居中
    
    /* 批量创建 8 个字符的点阵网格 */
    for (int c = 0; c < 8; c++) {
        lv_obj_t *char_container = lv_obj_create(time_container);
        lv_obj_set_size(char_container, 38, 54);
        lv_obj_set_style_bg_opa(char_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(char_container, 0, 0);
        lv_obj_set_style_pad_all(char_container, 0, 0);
        /* 设置水平排列 X 轴偏移 */
        lv_obj_set_pos(char_container, c * (38 + CHAR_GAP), 0);
        
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 7; y++) {
                lv_obj_t *dot = lv_obj_create(char_container);
                lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
                /* 设置像素点网格内相对坐标 */
                lv_obj_set_pos(dot, x * (DOT_SIZE + DOT_GAP), y * (DOT_SIZE + DOT_GAP));
                /* 样式使像素点为圆形，并默认亮起暗灰色 */
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                lv_obj_set_style_bg_color(dot, lv_color_make(26, 26, 26), 0); // 暗色底 `#1A1A1A`
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                
                dot_objs[c][x][y] = dot;
            }
        }
    }

    /* 构建 60 秒极坐标圆形矩阵点阵 */
    for (int i = 0; i < RING_DOTS; i++) {
        /* 每一个点对应极坐标角度，以正上方为 0 秒 (theta = -90度) */
        float theta = (i * 6.0f - 90.0f) * M_PI / 180.0f;
        int x_pos = (int)(240.0f + RING_RADIUS * cosf(theta)) - 2; // 圆点半径为2 (大小4像素)，做中心补偿
        int y_pos = (int)(240.0f + RING_RADIUS * sinf(theta)) - 2;
        
        lv_obj_t *ring_dot = lv_obj_create(scr);
        lv_obj_set_size(ring_dot, 4, 4);
        lv_obj_set_pos(ring_dot, x_pos, y_pos);
        
        lv_obj_set_style_radius(ring_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring_dot, 0, 0);
        lv_obj_set_style_bg_color(ring_dot, lv_color_make(38, 38, 38), 0); // 边缘秒环默认底色 `#262626`
        lv_obj_set_style_bg_opa(ring_dot, LV_OPA_COVER, 0);
        
        ring_dots[i] = ring_dot;
    }

    uint32_t last_rtc_update = 0;
    uint8_t hour = 0, min = 0, sec = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        /* Update RTC time every 1000ms */
        if (now - last_rtc_update >= 1000 || last_rtc_update == 0) {
            if (rtc_pcf85063_get_time(&hour, &min, &sec) == ESP_OK) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, sec);
                ESP_LOGI(TAG, "Updated display: %s", buf);
                
                /* 1. 刷新 8 个点阵字符 */
                for (int c = 0; c < 8; c++) {
                    char ch = buf[c];
                    int font_idx = 0;
                    if (ch >= '0' && ch <= '9') {
                        font_idx = ch - '0';
                    } else if (ch == ':') {
                        font_idx = 10;
                    } else {
                        continue;
                    }
                    
                    /* 读取该字模，并将对应的点亮起或熄灭 */
                    for (int x = 0; x < 5; x++) {
                        for (int y = 0; y < 7; y++) {
                            // 取出第 y 行的第 4-x 位（左起第一列是 bit 4）
                            bool is_lit = (font_5x7[font_idx][y] >> (4 - x)) & 1;
                            lv_obj_t *dot = dot_objs[c][x][y];
                            if (is_lit) {
                                lv_obj_set_style_bg_color(dot, lv_color_make(229, 229, 229), 0); // 浅浅冷灰 `#E5E5E5`
                            } else {
                                lv_obj_set_style_bg_color(dot, lv_color_make(26, 26, 26), 0);    // 暗底灰 `#1A1A1A`
                            }
                        }
                    }
                }
                
                /* 2. 刷新秒环红点秒针 */
                static int last_sec = -1;
                if (last_sec != -1 && last_sec < RING_DOTS) {
                    // 恢复上一次秒点为普通暗灰圆环色，尺寸 4x4
                    lv_obj_set_size(ring_dots[last_sec], 4, 4);
                    lv_obj_set_style_bg_color(ring_dots[last_sec], lv_color_make(38, 38, 38), 0);
                    
                    // 重新微调坐标，补偿 4x4 居中偏移 (圆心位置补偿 225 半径)
                    float last_theta = (last_sec * 6.0f - 90.0f) * (float)M_PI / 180.0f;
                    int lx = (int)((LCD_WIDTH / 2.0f) + RING_RADIUS * cosf(last_theta)) - 2;
                    int ly = (int)((LCD_HEIGHT / 2.0f) + RING_RADIUS * sinf(last_theta)) - 2;
                    lv_obj_set_pos(ring_dots[last_sec], lx, ly);
                }
                
                if (sec < RING_DOTS) {
                    // 将当前秒点设为 Nothing 标志性红点，尺寸变大为 6x6
                    lv_obj_set_size(ring_dots[sec], 6, 6);
                    lv_obj_set_style_bg_color(ring_dots[sec], lv_color_make(255, 0, 64), 0); // 亮红 `#FF0040`
                    
                    // 重新微调坐标，补偿 6x6 居中偏移
                    float curr_theta = (sec * 6.0f - 90.0f) * (float)M_PI / 180.0f;
                    int cx = (int)((LCD_WIDTH / 2.0f) + RING_RADIUS * cosf(curr_theta)) - 3;
                    int cy = (int)((LCD_HEIGHT / 2.0f) + RING_RADIUS * sinf(curr_theta)) - 3;
                    lv_obj_set_pos(ring_dots[sec], cx, cy);
                    
                    last_sec = sec;
                }
            } else {
                ESP_LOGE(TAG, "Failed to read RTC for display update");
            }
            last_rtc_update = now;
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    pmic_init();
    rtc_pcf85063_init();  /* Initialize RTC */
    lcd_init();           /* Initialize LCD + LVGL */

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
}
