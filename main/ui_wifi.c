#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <esp_log.h>
#include "lvgl.h"
#include "ui_wifi.h"
#include "ui_settings.h"
#include "ui_keyboard.h"
#include "bsp_lcd.h"
#include "app_wifi.h"

#define TAG "ui_wifi"

static char s_target_ssid[33];   /* 键盘回调时连接的目标网络 */
static void kb_on_ok(const char *password, void *user_data);

#define WIFI_ROW_W      424
#define WIFI_ROW_H      64
#define WIFI_ROW_X      28
#define WIFI_LIST_Y     96

static lv_obj_t *s_wifi_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_rows[20];
static size_t s_row_count = 0;
static lv_timer_t *s_poll_timer = NULL;

/* 上次呈现的状态，避免每 200ms 重设文本 */
static app_wifi_state_t s_last_state = APP_WIFI_STATE_IDLE;
static size_t s_last_ap_count = 0;

static void status_color_update(app_wifi_state_t st)
{
    lv_color_t c = lv_color_hex(0xBBBBBB);
    if (st == APP_WIFI_STATE_DONE) {
        c = lv_color_hex(0x4CAF50);
    } else if (st == APP_WIFI_STATE_FAILED) {
        c = lv_color_hex(0xE57373);
    } else if (st == APP_WIFI_STATE_SCANNING || st == APP_WIFI_STATE_CONNECTING ||
               st == APP_WIFI_STATE_SYNCING) {
        c = lv_color_white();
    }
    lv_obj_set_style_text_color(s_status_label, c, 0);
}

static void rebuild_list(void)
{
    const app_wifi_status_t *st = app_wifi_get_status();

    /* 清旧行 */
    for (size_t i = 0; i < s_row_count; i++) {
        lv_obj_del(s_rows[i]);
        s_rows[i] = NULL;
    }
    s_row_count = 0;

    if (st->ap_count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_obj_set_pos(empty, 24, 16);
        lv_label_set_text(empty, "No networks found");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x777777), 0);
        s_rows[s_row_count++] = empty; /* 复用删除机制 */
        return;
    }

    for (size_t i = 0; i < st->ap_count && s_row_count < 20; i++) {
        const app_wifi_ap_info_t *ap = &st->ap_list[i];
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, WIFI_ROW_W, WIFI_ROW_H);
        lv_obj_set_pos(row, 0, (lv_coord_t)(i * (WIFI_ROW_H + 8)));
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x333333), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ssid = lv_label_create(row);
        lv_obj_set_pos(ssid, 20, 8);
        lv_label_set_text(ssid, ap->ssid); /* 中文 SSID 显示方块，字节透传可连接 */
        lv_obj_set_style_text_font(ssid, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ssid, lv_color_white(), 0);
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid, 280);

        char info[24];
        snprintf(info, sizeof(info), "%d dBm %s%s", ap->rssi,
                 ap->auth_open ? "OPEN" : "", ap->saved ? " *" : "");
        lv_obj_t *meta = lv_label_create(row);
        lv_obj_set_pos(meta, 20, 38);
        lv_label_set_text(meta, info);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(meta, lv_color_hex(0x999999), 0);

        s_rows[s_row_count++] = row;
    }
}

static void kb_on_ok(const char *password, void *user_data)
{
    (void)user_data;
    app_wifi_cmd_connect(s_target_ssid, password);
}

static void row_clicked(size_t idx)
{
    const app_wifi_status_t *st = app_wifi_get_status();
    if (idx >= st->ap_count) {
        return;
    }
    /* 连接/同步进行中忽略点击 */
    if (st->state == APP_WIFI_STATE_CONNECTING || st->state == APP_WIFI_STATE_SYNCING ||
        st->state == APP_WIFI_STATE_SCANNING) {
        return;
    }
    const app_wifi_ap_info_t *ap = &st->ap_list[idx];
    if (ap->auth_open) {
        app_wifi_cmd_connect(ap->ssid, "");
    } else {
        char pwd[65];
        if (app_wifi_load_password(ap->ssid, pwd, sizeof(pwd))) {
            app_wifi_cmd_connect(ap->ssid, NULL); /* 有保存密码直接连 */
        } else {
            /* 弹 T9 键盘 */
            strlcpy(s_target_ssid, ap->ssid, sizeof(s_target_ssid));
            const ui_keyboard_cfg_t cfg = {
                .title = s_target_ssid,
                .on_ok = kb_on_ok,
                .on_cancel = NULL,
            };
            ui_keyboard_open(s_wifi_screen, &cfg);
        }
    }
}

static void list_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *target = lv_event_get_target(e);
    for (size_t i = 0; i < s_row_count; i++) {
        if (target == s_rows[i]) {
            row_clicked(i);
            return;
        }
    }
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_settings_show();
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    app_wifi_cmd_scan();
}

static void wifi_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            ui_settings_show();
        }
    }
}

static void poll_timer_cb(lv_timer_t *t)
{
    if (lv_scr_act() != s_wifi_screen) {
        return;
    }
    const app_wifi_status_t *st = app_wifi_get_status();

    if (strcmp(lv_label_get_text(s_status_label), st->status_text) != 0) {
        lv_label_set_text(s_status_label, st->status_text[0] ? st->status_text : " ");
        status_color_update(st->state);
    }
    if (st->state == APP_WIFI_STATE_IDLE && st->ap_count != s_last_ap_count) {
        s_last_ap_count = st->ap_count;
        rebuild_list();
    }
    s_last_state = st->state;
    (void)t;
}

void ui_wifi_create(void)
{
    s_wifi_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wifi_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_wifi_screen, wifi_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 标题 + 返回 */
    lv_obj_t *back = lv_btn_create(s_wifi_screen);
    lv_obj_set_size(back, 44, 44);
    lv_obj_set_pos(back, 28, 16);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_l = lv_label_create(back);
    lv_label_set_text(back_l, "<");
    lv_obj_set_style_text_font(back_l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_l, lv_color_white(), 0);
    lv_obj_center(back_l);

    lv_obj_t *title = lv_label_create(s_wifi_screen);
    lv_obj_set_pos(title, 84, 24);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    /* 刷新按钮 */
    lv_obj_t *refresh = lv_btn_create(s_wifi_screen);
    lv_obj_set_size(refresh, 100, 36);
    lv_obj_set_pos(refresh, 352, 20);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(refresh, 8, 0);
    lv_obj_set_style_shadow_width(refresh, 0, 0);
    lv_obj_set_style_border_width(refresh, 0, 0);
    lv_obj_add_event_cb(refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_l = lv_label_create(refresh);
    lv_label_set_text(refresh_l, "SCAN");
    lv_obj_set_style_text_font(refresh_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(refresh_l, lv_color_white(), 0);
    lv_obj_center(refresh_l);

    /* 状态行 */
    s_status_label = lv_label_create(s_wifi_screen);
    lv_obj_set_pos(s_status_label, 28, 64);
    lv_label_set_text(s_status_label, " ");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xBBBBBB), 0);

    /* 可滚动列表容器 */
    s_list = lv_obj_create(s_wifi_screen);
    lv_obj_set_size(s_list, WIFI_ROW_W, 480 - WIFI_LIST_Y - 8);
    lv_obj_set_pos(s_list, WIFI_ROW_X, WIFI_LIST_Y);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_add_event_cb(s_list, list_event_cb, LV_EVENT_CLICKED, NULL);

    s_poll_timer = lv_timer_create(poll_timer_cb, 200, NULL);
    (void)s_poll_timer;
    (void)s_last_state;
}

void ui_wifi_show(void)
{
    bsp_lcd_set_flush_mode(BSP_LCD_FLUSH_PASSTHROUGH);
    lv_scr_load_anim(s_wifi_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 250, 0, false);
    app_wifi_ensure_started();
    app_wifi_cmd_scan();
}
