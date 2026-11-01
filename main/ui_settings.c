#include <stdio.h>
#include <stdbool.h>
#include <esp_log.h>
#include "lvgl.h"
#include "ui_settings.h"
#include "ui_clock.h"
#include "ui_wifi.h"
#include "bsp_lcd.h"
#include "bsp_power.h"

#define TAG "ui_settings"

/* 页面导航请求：任意任务置位（volatile），gui_task 轮询消费 */
static volatile ui_nav_req_t s_nav_req = UI_NAV_NONE;

void ui_nav_request(ui_nav_req_t r)
{
    s_nav_req = r;
}

ui_nav_req_t ui_nav_poll_request(void)
{
    ui_nav_req_t r = s_nav_req;
    s_nav_req = UI_NAV_NONE;
    return r;
}

static lv_obj_t *s_settings_screen = NULL;
static lv_obj_t *s_battery_fill = NULL;
static lv_obj_t *s_battery_label = NULL;
static lv_obj_t *s_bright_label = NULL;
static lv_timer_t *s_battery_timer = NULL;

static void wifi_row_click_cb(lv_event_t *e)
{
    (void)e;
    ui_wifi_show();
}

/* 电池图形几何常量：外壳 + 内部电量条 */
#define BAT_SHELL_W     120
#define BAT_SHELL_H     30
#define BAT_FILL_PAD    3

static void settings_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            /* 下滑返回时钟页 */
            ui_clock_show();
        }
    }
}

static lv_obj_t *row_create(lv_obj_t *parent, int y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 424, 88);
    lv_obj_set_pos(row, 28, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x333333), LV_STATE_PRESSED);
    return row;
}

static lv_obj_t *row_label_create(lv_obj_t *row, const char *text)
{
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_pos(label, 24, 10);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    return label;
}

static void brightness_slider_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *slider = lv_event_get_target(e);
        uint8_t pct = (uint8_t)lv_slider_get_value(slider);
        bsp_lcd_set_brightness_pct(pct);
        lv_label_set_text_fmt(s_bright_label, "%u%%", pct);
    }
}

static void battery_timer_cb(lv_timer_t *timer)
{
    /* 仅设置页可见时刷新，避免无谓 I2C 读取 */
    if (lv_scr_act() != s_settings_screen) {
        return;
    }
    ui_settings_refresh_battery();
}

void ui_settings_refresh_battery(void)
{
    uint8_t pct = 0;
    if (bsp_power_get_battery_pct(&pct) != ESP_OK) {
        return;
    }
    lv_label_set_text_fmt(s_battery_label, "%u%%", pct);

    int inner_w = BAT_SHELL_W - 2 * BAT_FILL_PAD - 4; /* 右侧留 4px 与极耳间隔 */
    lv_obj_set_width(s_battery_fill, (pct * inner_w) / 100);
}

void ui_settings_create(void)
{
    s_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_settings_screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_settings_screen, settings_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_settings_screen);
    lv_obj_set_pos(title, 28, 36);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    /* 行 1：WiFi → WiFi 子页 */
    lv_obj_t *wifi_row = row_create(s_settings_screen, 110);
    row_label_create(wifi_row, "WiFi");
    lv_obj_add_event_cb(wifi_row, wifi_row_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chevron = lv_label_create(wifi_row);
    lv_obj_set_pos(chevron, 376, 26);
    lv_label_set_text(chevron, ">");
    lv_obj_set_style_text_font(chevron, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(chevron, lv_color_hex(0x999999), 0);

    /* 行 2：亮度滑条 10-100% */
    lv_obj_t *bright_row = row_create(s_settings_screen, 214);
    row_label_create(bright_row, "Brightness");
    s_bright_label = lv_label_create(bright_row);
    lv_obj_set_pos(s_bright_label, 24, 48);
    lv_label_set_text_fmt(s_bright_label, "%u%%", bsp_lcd_get_brightness_pct());
    lv_obj_set_style_text_font(s_bright_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bright_label, lv_color_hex(0xBBBBBB), 0);

    lv_obj_t *slider = lv_slider_create(bright_row);
    lv_obj_set_width(slider, 220);
    lv_obj_set_pos(slider, 170, 32);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, bsp_lcd_get_brightness_pct(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 行 3：电量（百分比 + 分段电池图形，每 5s 刷新） */
    lv_obj_t *bat_row = row_create(s_settings_screen, 318);
    row_label_create(bat_row, "Battery");
    s_battery_label = lv_label_create(bat_row);
    lv_obj_set_pos(s_battery_label, 24, 48);
    lv_label_set_text(s_battery_label, "--%");
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0xBBBBBB), 0);

    lv_obj_t *shell = lv_obj_create(bat_row);
    lv_obj_set_size(shell, BAT_SHELL_W, BAT_SHELL_H);
    lv_obj_set_pos(shell, 264, 28);
    lv_obj_set_style_border_color(shell, lv_color_white(), 0);
    lv_obj_set_style_border_width(shell, 2, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(shell, 4, 0);
    lv_obj_set_style_pad_all(shell, 0, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_battery_fill = lv_obj_create(shell);
    lv_obj_set_height(s_battery_fill, BAT_SHELL_H - 2 * BAT_FILL_PAD - 4);
    lv_obj_set_pos(s_battery_fill, BAT_FILL_PAD, BAT_FILL_PAD);
    lv_obj_set_width(s_battery_fill, 0);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_battery_fill, 2, 0);
    lv_obj_set_style_border_width(s_battery_fill, 0, 0);
    lv_obj_set_style_pad_all(s_battery_fill, 0, 0);
    lv_obj_clear_flag(s_battery_fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 底部提示 */
    lv_obj_t *hint = lv_label_create(s_settings_screen);
    lv_obj_set_pos(hint, 28, 436);
    lv_label_set_text(hint, "Swipe down to return");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);

    s_battery_timer = lv_timer_create(battery_timer_cb, 5000, NULL);
    (void)s_battery_timer;
    ui_settings_refresh_battery();
}

void ui_settings_show(void)
{
    bsp_lcd_set_flush_mode(BSP_LCD_FLUSH_PASSTHROUGH);
    lv_scr_load_anim(s_settings_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 250, 0, false);
    ui_settings_refresh_battery();
}
