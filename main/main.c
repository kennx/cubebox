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
#include <driver/gpio.h>

LV_FONT_DECLARE(lv_font_montserrat_48);

// 5x7 点阵字模: 0-9、冒号(:)、减号(-)、空格以及 13 个大写英文字母
static const uint8_t font_5x7[39][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0: 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x01, 0x02, 0x0E, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}, // 9
    {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}, // 10: 冒号 (:)
    {0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00}, // 11: 减号 (-)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 12: 空格 ( )
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // 13: A
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // 14: B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // 15: C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // 16: D
    {0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x1F}, // 17: E
    {0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x10}, // 18: F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // 19: G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // 20: H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 21: I
    {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}, // 22: J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // 23: K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // 24: L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // 25: M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // 26: N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 27: O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // 28: P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // 29: Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // 30: R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // 31: S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // 32: T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 33: U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // 34: V
    {0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x11}, // 35: W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // 36: X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // 37: Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}  // 38: Z
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
#define AXP2101_LDO_ONOFF_CTRL1     0x91
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

static char current_time_str[16] = "00:00:00";
static char current_date_str[16] = "06-01 MON"; // 全局日期字符串

static int char_to_font_idx(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == ':') return 10;
    if (ch == '-') return 11;
    if (ch == ' ') return 12;
    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A'; // 小写转大写
    if (ch >= 'A' && ch <= 'Z') return 13 + (ch - 'A');
    return 12; // 默认空格
}

static esp_err_t rtc_pcf85063_write_reg(uint8_t reg, uint8_t val)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pcf85063_dev, buf, 2, pdMS_TO_TICKS(1000));
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

    /* Force 24H mode by clearing bit 2 in Control_1 (0x00) */
    ESP_ERROR_CHECK(rtc_pcf85063_write_reg(0x00, 0x00));
    ESP_LOGI(TAG, "PCF85063 RTC forced to 24H mode");
}


static const char *weekdays_str[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *months_str[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static esp_err_t rtc_pcf85063_get_datetime(uint8_t *hour, uint8_t *minute, uint8_t *second,
                                           uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year)
{
    if (pcf85063_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_addr = 0x04; /* Seconds register address */
    uint8_t data[7] = {0};
    
    /* Read 7 bytes starting from 0x04: Seconds, Minutes, Hours, Days, Weekdays, Months, Years */
    esp_err_t ret = i2c_master_transmit_receive(pcf85063_dev, &reg_addr, 1, data, 7, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        return ret;
    }
    
    *second  = bcd2dec(data[0] & 0x7F);
    *minute  = bcd2dec(data[1] & 0x7F);
    *hour    = bcd2dec(data[2] & 0x3F);
    *day     = bcd2dec(data[3] & 0x3F);
    *weekday = bcd2dec(data[4] & 0x07);
    *month   = bcd2dec(data[5] & 0x1F);
    *year    = bcd2dec(data[6]);
    
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

    /* Enable DC1 (bit 0) and ALDO1/2/4 (bits 0/1/3). ALDO3 is toggled by LCD reset. */
    axp2101_set_bit(AXP2101_DC_ONOFF_DVM_CTRL, 0);
    uint8_t ldo_onoff;
    ESP_ERROR_CHECK(axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &ldo_onoff));
    ldo_onoff |= 0x0B;
    ldo_onoff &= ~0xB4;
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_ONOFF_CTRL0, ldo_onoff));

    uint8_t ldo_onoff1;
    ESP_ERROR_CHECK(axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL1, &ldo_onoff1));
    ldo_onoff1 &= ~0x03;
    ESP_ERROR_CHECK(axp2101_write_reg(AXP2101_LDO_ONOFF_CTRL1, ldo_onoff1));

    /* Verify registers */
    uint8_t dc_onoff, dc_vol, ldo_vol0, ldo_vol3;
    axp2101_read_reg(AXP2101_DC_ONOFF_DVM_CTRL, &dc_onoff);
    axp2101_read_reg(AXP2101_DC_VOL0_CTRL, &dc_vol);
    axp2101_read_reg(AXP2101_LDO_VOL0_CTRL, &ldo_vol0);
    axp2101_read_reg(AXP2101_LDO_VOL3_CTRL, &ldo_vol3);
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL0, &ldo_onoff);
    axp2101_read_reg(AXP2101_LDO_ONOFF_CTRL1, &ldo_onoff1);
    ESP_LOGI(TAG, "PMIC verify: DC_ONOFF=0x%02X DC_VOL=0x%02X LDO0_VOL=0x%02X LDO3_VOL=0x%02X LDO_ONOFF=0x%02X LDO_ONOFF1=0x%02X",
             dc_onoff, dc_vol, ldo_vol0, ldo_vol3, ldo_onoff, ldo_onoff1);

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

// 经典两端对齐双行纯点阵时钟与日期常量定义
#define DATE_GRID_SIZE      5      // 日期网格 5x5px
#define DATE_COLS           15     // 日期总字符数 15 ("MON JUN 01 2026")
#define DATE_ROWS           7      // 日期总行数 7
#define DATE_OFFSET_X       28     // 左偏移 28px
#define DATE_OFFSET_Y       170    // 日期 Y 轴起始：170px

#define TIME_GRID_SIZE      9      // 时间网格 9x9px
#define TIME_COLS           47     // 时间总网格列数 47
#define TIME_ROWS           7      // 时间总网格行数 7
#define TIME_OFFSET_X       28     // 左偏移 28px
#define TIME_OFFSET_Y       229    // 时间 Y 轴起始：229px

// 15 个日期字符在 423px 水平框内的起始 X 轴局部坐标表（完美实现两端 423px 对齐）
static const int date_char_x[15] = {
    0, 29, 57, 85, 114, 142, 170, 199, 227, 255, 284, 312, 340, 369, 398
};

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
        ESP_LOGE("main", "Flush size too large: %lu, max: %d", size, LCD_WIDTH * 60);
        lv_disp_flush_ready(drv);
        return;
    }

    for (uint32_t y = 0; y < height; y++) {
        int global_y = offsety1 + y;
        for (uint32_t x = 0; x < width; x++) {
            int global_x = offsetx1 + x;
            uint32_t i = y * width + x;

            int dx = global_x - DATE_OFFSET_X;
            int dy = global_y - DATE_OFFSET_Y;

            int tx = global_x - TIME_OFFSET_X;
            int ty = global_y - TIME_OFFSET_Y;

            if (dx >= 0 && dx < 423 && dy >= 0 && dy < DATE_ROWS * DATE_GRID_SIZE) {
                // 1. 渲染上方日期：MON JUN 01 2026 (网格 5px，圆点 4px，间距 1px)
                int lx = dx % DATE_GRID_SIZE;
                int ly = dy % DATE_GRID_SIZE;

                if (lx < 4 && ly < 4) {
                    bool is_lit = false;
                    int char_idx = -1;
                    
                    // 查表确定落在 15 个字符的哪一个区间内
                    for (int c = 0; c < 15; c++) {
                        if (dx >= date_char_x[c] && dx < date_char_x[c] + 25) {
                            char_idx = c;
                            break;
                        }
                    }

                    if (char_idx >= 0) {
                        int char_x_offset = dx - date_char_x[char_idx];
                        int char_col = char_x_offset / 5; // 0 到 4

                        if (char_col < 5) {
                            char ch = current_date_str[char_idx];
                            int font_idx = char_to_font_idx(ch);
                            if (font_idx >= 0) {
                                int grid_y = dy / DATE_GRID_SIZE; // 0 到 6
                                is_lit = (font_5x7[font_idx][grid_y] >> (4 - char_col)) & 1;
                            }
                        }
                    }
                    swap_buf[i] = is_lit ? lv_color_make(229, 229, 229) : lv_color_black();
                } else {
                    swap_buf[i] = color_map[i]; // 1px 细线间距透传
                }
            }
            else if (tx >= 0 && tx < TIME_COLS * TIME_GRID_SIZE &&
                     ty >= 0 && ty < TIME_ROWS * TIME_GRID_SIZE) {
                
                // 2. 渲染下方时间：HH:MM:SS (网格 9px，圆点 8px，间距 1px)
                int grid_x = tx / TIME_GRID_SIZE;
                int grid_y = ty / TIME_GRID_SIZE;
                int lx = tx % TIME_GRID_SIZE;
                int ly = ty % TIME_GRID_SIZE;

                if (lx < 8 && ly < 8) {
                    bool is_lit = false;
                    int char_idx = grid_x / 6;
                    int char_col = grid_x % 6;

                    if (char_col < 5) {
                        char ch = current_time_str[char_idx];
                        int font_idx = char_to_font_idx(ch);
                        if (font_idx >= 0) {
                            is_lit = (font_5x7[font_idx][grid_y] >> (4 - char_col)) & 1;
                        }
                    }
                    swap_buf[i] = is_lit ? lv_color_make(229, 229, 229) : lv_color_black();
                } else {
                    swap_buf[i] = color_map[i]; // 1px 细线间距透传
                }
            } 
            else {
                swap_buf[i] = color_map[i]; // 区域外直接透传 LVGL 原始像素
            }
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
    ESP_ERROR_CHECK(lcd_power_reset());
    
    ESP_LOGI(TAG, "Initializing LCD panel...");
    ret = esp_lcd_panel_init(lcd_panel);
    ESP_LOGI(TAG, "esp_lcd_panel_init result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Turning LCD display on...");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    lcd_set_backlight(100);
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

static void gui_task(void *arg)
{
    /* Set screen background to black */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    uint32_t last_rtc_update = 0;
    uint8_t hour = 0, min = 0, sec = 0;
    uint8_t day = 1, weekday = 1, month = 6, year = 26;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        /* Update RTC time and date every 1000ms */
        if (now - last_rtc_update >= 1000 || last_rtc_update == 0) {
            if (rtc_pcf85063_get_datetime(&hour, &min, &sec, &day, &weekday, &month, &year) == ESP_OK) {
                // 越界安全防护
                if (weekday > 6) weekday = 1;
                if (month < 1 || month > 12) month = 6;
                if (day < 1 || day > 31) day = 1;
                if (year > 99) year = 26;

                snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d:%02d", hour, min, sec);
                snprintf(current_date_str, sizeof(current_date_str), "%s %s %02d 20%02d", 
                         weekdays_str[weekday], months_str[month - 1], day, year);
                
                ESP_LOGI(TAG, "Updated display: %s | %s", current_date_str, current_time_str);
                
                // Force full screen redraw in LVGL to flush using our global grid rendering
                lv_obj_invalidate(lv_scr_act());
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

    pmic_init();
    rtc_pcf85063_init();  /* Initialize RTC */
    lcd_init();           /* Initialize LCD + LVGL */

    xTaskCreate(gui_task, "gui_task", 4096 * 2, NULL, 5, NULL);
}
