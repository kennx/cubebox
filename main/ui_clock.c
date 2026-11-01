#include <stdio.h>
#include <stdbool.h>
#include <esp_log.h>
#include "lvgl.h"
#include "ui_clock.h"
#include "bsp_rtc.h"

#define TAG "ui_clock"

// 5x7 点阵字模: 0-9、冒号(:)、减号(-)、空格以及 26 个大写英文字母
static const uint8_t font_5x7[39][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0: 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1: 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2: 2
    {0x1F, 0x01, 0x02, 0x0E, 0x01, 0x11, 0x0E}, // 3: 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4: 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5: 5
    {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6: 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7: 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8: 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}, // 9: 9
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

/* 顶部状态栏已按需求移除（WiFi 状态改在设置页显示） */

// 经典两端对齐双行纯点阵时钟与日期常量定义
#define DATE_GRID_SIZE      5      // 日期网格 5x5px
#define DATE_COLS           15     // 日期总字符数 15 ("MON JUN 01 2026")
#define DATE_ROWS           7      // 日期总行数 7
#define DATE_OFFSET_X       28     // 左偏移 28px
#define DATE_OFFSET_Y       170    // 日期 Y 轴起始：170px

#define TIME_GRID_SIZE      9      // 时间网格 9x9px
#define TIME_COLS           47     // 时间总网格列数 47
#define TIME_ROWS           7      // 时间总行数 7
#define TIME_OFFSET_X       28     // 左偏移 28px
#define TIME_OFFSET_Y       229    // 时间 Y 轴起始：229px

// 15 个日期字符在 423px 水平框内的起始 X 轴局部坐标表（完美实现两端 423px 对齐）
static const int date_char_x[15] = {
    0, 29, 57, 85, 114, 142, 170, 199, 227, 255, 284, 312, 340, 369, 398
};

static char current_time_str[16] = "00:00:00";
static char current_date_str[32] = "MON JUN 01 2026"; // 全局日期字符串，32B 留有余量

/**
 * @brief 字符到 font_5x7 点阵字模索引映射
 * @note 假设输入为 ASCII 编码；小写字母通过算术运算转换为大写
 */
static int char_to_font_idx(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == ':') return 10;
    if (ch == '-') return 11;
    if (ch == ' ') return 12;
    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A'; // ASCII 小写转大写
    if (ch >= 'A' && ch <= 'Z') return 13 + (ch - 'A');
    return 12; // 默认空格
}

void ui_clock_tick_refresh(void)
{
    uint8_t hour = 0, min = 0, sec = 0;
    uint8_t day = 1, weekday = 1, month = 6, year = 26;

    if (bsp_rtc_get_datetime(&hour, &min, &sec, &day, &weekday, &month, &year) == ESP_OK) {
        // 越界安全防护
        if (weekday > 6) weekday = 1;
        if (month < 1 || month > 12) month = 6;
        if (day < 1 || day > 31) day = 1;
        if (year > 99) year = 26;

        snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d:%02d", hour, min, sec);
        snprintf(current_date_str, sizeof(current_date_str), "%s %s %02d 20%02d",
                 bsp_rtc_weekdays_str[weekday], bsp_rtc_months_str[month - 1], day, year);

        ESP_LOGI(TAG, "Updated display: %s | %s", current_date_str, current_time_str);

        /*
         * Full-screen invalidate is required because ui_clock_overlay_apply intercepts
         * pixel rendering for the dot-matrix clock. LVGL has no
         * knowledge of these custom pixel overlays, so we must force a flush
         * across the entire screen to ensure the dot-matrix regions are redrawn.
         * On AMOLED this costs extra power; see AGENTS.md for the exception note.
         */
        lv_obj_invalidate(lv_scr_act());
    } else {
        ESP_LOGE(TAG, "Failed to read RTC for display update");
    }
}

void ui_clock_overlay_apply(const lv_area_t *area, const lv_color_t *src, lv_color_t *dst)
{
    const int offsetx1 = area->x1;
    const int offsety1 = area->y1;
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

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
                    dst[i] = is_lit ? lv_color_make(229, 229, 229) : src[i];
                } else {
                    dst[i] = src[i]; // 1px 细线间距透传
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
                    dst[i] = is_lit ? lv_color_make(229, 229, 229) : src[i];
                } else {
                    dst[i] = src[i]; // 1px 细线间距透传
                }
            }
            else {
                dst[i] = src[i]; // 区域外直接透传 LVGL 原始像素
            }
        }
    }
}
