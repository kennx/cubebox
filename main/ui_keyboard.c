#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <esp_log.h>
#include "lvgl.h"
#include "ui_keyboard.h"

#define TAG "ui_keyboard"

#define KBD_BUF_MAX     64
#define KBD_COMMIT_MS   800
#define KBD_LONGPRESS_MS 600

/* 键位索引（行优先）：
 * 0:1  1:2  2:3
 * 3:4  4:5  5:6
 * 6:7  7:8  8:9
 * 9:#+=/abc  10:0/␣  11:⌫
 */
#define KEY_IDX_SYM     9
#define KEY_IDX_ZERO    10
#define KEY_IDX_BACK    11

/* 字母页循环集（含数字本身，经典 T9） */
static const char *const letters_set[10] = {
    ".,?!1",                       /* 1 */
    "abc2", "def3", "ghi4", "jkl5", "mno6",
    "pqrs7", "tuv8", "wxyz9",
    "0 ",                          /* 0：先数字后空格 */
};

/* 符号页 1-9 键面（单击直入，无循环） */
static const char sym_page[9] = { '.', ',', '?', '!', '-', '_', '+', ':', '@' };

static const char *const key_labels[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "#+=", "0", "\xE2\x8C\xAB" /* ⌫ */
};
static const char *const key_sublabels[12] = {
    ".,?", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz", "abc", "\xE2\x90\xA3" /* ␣ */, ""
};

/* ---- 状态 ---- */
static char s_buf[KBD_BUF_MAX + 1];
static size_t s_buf_len = 0;
static int s_pending_key = -1;      /* 正在多击的键（0-10），-1 无 */
static int s_pending_idx = 0;
static bool s_sym_page = false;
static bool s_upper = false;

static lv_obj_t *s_kb = NULL;           /* 模态容器 */
static lv_obj_t *s_key_btns[12];
static lv_obj_t *s_pw_label = NULL;
static lv_obj_t *s_pw_box = NULL;
static lv_obj_t *s_shift_btn = NULL;
static lv_timer_t *s_commit_timer = NULL;
static ui_keyboard_cfg_t s_cfg;

/* ---- 显示刷新 ---- */

static void refresh_pw_label(void)
{
    if (s_pw_label == NULL) {
        return;
    }
    static char show[KBD_BUF_MAX + 3];
    memcpy(show, s_buf, s_buf_len);
    show[s_buf_len] = '_';            /* 光标 */
    show[s_buf_len + 1] = '\0';
    lv_label_set_text(s_pw_label, show[0] ? show : "_");
}

static void refresh_key_labels(void)
{
    for (int i = 0; i < 12; i++) {
        lv_obj_t *main = lv_obj_get_child(s_key_btns[i], 0);
        lv_obj_t *sub = lv_obj_get_child(s_key_btns[i], 1);
        if (s_sym_page) {
            if (i < 9) {
                char one[2] = { sym_page[i], '\0' };
                lv_label_set_text(main, one);
                lv_label_set_text(sub, "");
            } else if (i == KEY_IDX_SYM) {
                lv_label_set_text(main, "abc");
                lv_label_set_text(sub, "");
            } else if (i == KEY_IDX_ZERO) {
                lv_label_set_text(main, "~");
                lv_label_set_text(sub, "");
            }
            /* ⌫ 键面不变 */
        } else {
            lv_label_set_text(main, key_labels[i]);
            lv_label_set_text(sub, key_sublabels[i]);
        }
    }
}

static void refresh_shift_style(void)
{
    lv_obj_set_style_bg_color(s_shift_btn,
                              s_upper ? lv_color_white() : lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_shift_btn, 0),
                                s_upper ? lv_color_black() : lv_color_white(), 0);
}

/* ---- 多击引擎 ---- */

static void commit_pending(void)
{
    s_pending_key = -1;
    if (s_commit_timer != NULL) {
        lv_timer_pause(s_commit_timer);
    }
}

/* 超长提示：边框变红，下一次输入尝试时恢复白色 */
static void overflow_hint(void)
{
    lv_obj_set_style_border_color(s_pw_box, lv_color_hex(0xCC3333), 0);
}

static void apply_shift(char *ch)
{
    if (s_upper && *ch >= 'a' && *ch <= 'z') {
        *ch = (char)(*ch - 'a' + 'A');
    }
}

static void key_tapped(int idx)
{
    if (s_kb == NULL) {
        return;
    }
    lv_obj_set_style_border_color(s_pw_box, lv_color_white(), 0); /* 清除超长红框 */

    if (idx == KEY_IDX_BACK) {
        commit_pending();
        if (s_buf_len > 0) {
            s_buf[--s_buf_len] = '\0';
        }
        refresh_pw_label();
        return;
    }
    if (idx == KEY_IDX_SYM) {
        commit_pending();
        s_sym_page = !s_sym_page;
        refresh_key_labels();
        return;
    }

    if (s_sym_page) {
        /* 符号页：单击直入 */
        commit_pending();
        if (s_buf_len >= KBD_BUF_MAX) {
            overflow_hint();
            return;
        }
        char ch = (idx == KEY_IDX_ZERO) ? '~' : sym_page[idx];
        s_buf[s_buf_len++] = ch;
        s_buf[s_buf_len] = '\0';
        refresh_pw_label();
        return;
    }

    /* 字母页：1-9 与 0 多击循环（键 0 的循环集在 letters_set[9]） */
    const char *set = letters_set[(idx == KEY_IDX_ZERO) ? 9 : idx];
    size_t set_len = strlen(set);

    if (s_pending_key == idx) {
        s_pending_idx = (s_pending_idx + 1) % set_len;
        if (s_buf_len > 0) {
            char ch = set[s_pending_idx];
            apply_shift(&ch);
            s_buf[s_buf_len - 1] = ch;   /* 替换待定字符 */
        }
    } else {
        commit_pending();
        if (s_buf_len >= KBD_BUF_MAX) {
            overflow_hint();
            return;
        }
        s_pending_key = idx;
        s_pending_idx = 0;
        char ch = set[0];
        apply_shift(&ch);
        s_buf[s_buf_len++] = ch;
        s_buf[s_buf_len] = '\0';
    }
    refresh_pw_label();
    if (s_commit_timer != NULL) {
        lv_timer_reset(s_commit_timer);
        lv_timer_resume(s_commit_timer);
    }
}

static void commit_timer_cb(lv_timer_t *t)
{
    (void)t;
    commit_pending();
}

static void clear_all(void)
{
    s_buf_len = 0;
    s_buf[0] = '\0';
    commit_pending();
    refresh_pw_label();
}

static void key_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    for (int i = 0; i < 12; i++) {
        if (target == s_key_btns[i]) {
            if (code == LV_EVENT_CLICKED) {
                key_tapped(i);
            } else if (code == LV_EVENT_LONG_PRESSED && i == KEY_IDX_BACK) {
                clear_all();
            }
            return;
        }
    }
}

static void shift_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_upper = !s_upper;
        refresh_shift_style();
    }
}

static void ok_btn_cb(lv_event_t *e)
{
    (void)e;
    commit_pending();
    if (s_buf_len == 0) {
        overflow_hint();
        return;
    }
    ui_keyboard_cfg_t cfg = s_cfg;
    ui_keyboard_close();
    if (cfg.on_ok) {
        cfg.on_ok(s_buf, cfg.user_data);
    }
}

static void cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_keyboard_cfg_t cfg = s_cfg;
    ui_keyboard_close();
    if (cfg.on_cancel) {
        cfg.on_cancel(cfg.user_data);
    }
}

/* ---- 构建/销毁 ---- */

static lv_obj_t *key_btn_create(lv_obj_t *parent, int idx, int col, int row)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 140, 70);
    lv_obj_set_pos(btn, 18 + col * 152, 118 + row * 82);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_obj_t *main = lv_label_create(btn);
    lv_obj_set_style_text_font(main, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(main, lv_color_white(), 0);
    lv_obj_align(main, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *sub = lv_label_create(btn);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x999999), 0);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_add_event_cb(btn, key_btn_event_cb, LV_EVENT_CLICKED, NULL);
    if (idx == KEY_IDX_BACK) {
        lv_obj_add_event_cb(btn, key_btn_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
    return btn;
}

void ui_keyboard_open(lv_obj_t *parent_screen, const ui_keyboard_cfg_t *cfg)
{
    if (s_kb != NULL) {
        ui_keyboard_close();
    }
    s_cfg = *cfg;
    s_buf_len = 0;
    s_buf[0] = '\0';
    s_pending_key = -1;
    s_sym_page = false;
    s_upper = false;

    /* 模态容器：覆盖整屏、置顶、拦截下层输入 */
    s_kb = lv_obj_create(parent_screen);
    lv_obj_set_size(s_kb, 480, 480);
    lv_obj_set_pos(s_kb, 0, 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 0, 0);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_CLICKABLE); /* 拦截手势穿透 */

    /* 标题（目标 SSID，只读） */
    lv_obj_t *title = lv_label_create(s_kb);
    lv_obj_set_pos(title, 18, 14);
    lv_label_set_text_fmt(title, "WiFi: %s", cfg->title ? cfg->title : "");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xAAAAAA), 0);

    /* ⇧ 大小写切换 */
    s_shift_btn = lv_btn_create(s_kb);
    lv_obj_set_size(s_shift_btn, 44, 44);
    lv_obj_set_pos(s_shift_btn, 18, 44);
    lv_obj_set_style_radius(s_shift_btn, 8, 0);
    lv_obj_set_style_shadow_width(s_shift_btn, 0, 0);
    lv_obj_set_style_border_width(s_shift_btn, 0, 0);
    lv_obj_add_event_cb(s_shift_btn, shift_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *shift_label = lv_label_create(s_shift_btn);
    lv_label_set_text(shift_label, "\xE2\x87\xA7" /* ⇧ */);
    lv_obj_center(shift_label);
    lv_obj_set_style_text_color(shift_label, lv_color_white(), 0);

    /* 密码框（+光标） */
    s_pw_box = lv_obj_create(s_kb);
    lv_obj_set_size(s_pw_box, 396, 44);
    lv_obj_set_pos(s_pw_box, 70, 44);
    lv_obj_set_style_border_color(s_pw_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_pw_box, 2, 0);
    lv_obj_set_style_bg_opa(s_pw_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_pw_box, 8, 0);
    lv_obj_set_style_pad_all(s_pw_box, 0, 0);
    lv_obj_clear_flag(s_pw_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_pw_label = lv_label_create(s_pw_box);
    lv_obj_set_style_text_font(s_pw_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_pw_label, lv_color_white(), 0);
    lv_obj_align(s_pw_label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_label_set_long_mode(s_pw_label, LV_LABEL_LONG_DOT);

    /* 12 个键 */
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            s_key_btns[idx] = key_btn_create(s_kb, idx, col, row);
        }
    }
    refresh_key_labels();
    refresh_shift_style();
    refresh_pw_label();

    /* 底部 CANCEL / OK */
    lv_obj_t *cancel = lv_btn_create(s_kb);
    lv_obj_set_size(cancel, 140, 36);
    lv_obj_set_pos(cancel, 18, 440);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_set_style_border_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_l = lv_label_create(cancel);
    lv_label_set_text(cancel_l, "CANCEL");
    lv_obj_set_style_text_font(cancel_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cancel_l, lv_color_white(), 0);
    lv_obj_center(cancel_l);

    lv_obj_t *ok = lv_btn_create(s_kb);
    lv_obj_set_size(ok, 140, 36);
    lv_obj_set_pos(ok, 322, 440);
    lv_obj_set_style_bg_color(ok, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(ok, 8, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, ok_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_l = lv_label_create(ok);
    lv_label_set_text(ok_l, "OK");
    lv_obj_set_style_text_font(ok_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ok_l, lv_color_white(), 0);
    lv_obj_center(ok_l);

    s_commit_timer = lv_timer_create(commit_timer_cb, KBD_COMMIT_MS, NULL);
    lv_timer_pause(s_commit_timer);
}

void ui_keyboard_close(void)
{
    if (s_commit_timer != NULL) {
        lv_timer_del(s_commit_timer);
        s_commit_timer = NULL;
    }
    if (s_kb != NULL) {
        lv_obj_del(s_kb);
        s_kb = NULL;
    }
    s_pw_label = NULL;
    s_pw_box = NULL;
    s_shift_btn = NULL;
}

bool ui_keyboard_is_open(void)
{
    return s_kb != NULL;
}

/* ---- 调试/测试注入口 ---- */

void ui_keyboard_debug_tap(int key_idx)
{
    if (key_idx < 0 || key_idx > 11) {
        return;
    }
    key_tapped(key_idx);
}

void ui_keyboard_debug_state(char *out, size_t len)
{
    snprintf(out, len, "kbd buf='%s' len=%u pending_key=%d idx=%d page=%s upper=%d",
             s_buf, (unsigned)s_buf_len, s_pending_key, s_pending_idx,
             s_sym_page ? "sym" : "abc", s_upper);
}
