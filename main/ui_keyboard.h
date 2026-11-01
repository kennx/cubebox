#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

/**
 * @brief T9 九宫格密码键盘（多次点按循环选字符，经典手机布局）
 *
 * 字母页：1 .,? / 2 abc / ... / 9 wxyz / 0 ␣；循环集含数字本身（按 2 出 a→b→c→2）。
 * 同键连击循环，800ms 超时或按其他键提交；⇧ 粘性大小写切换；#+= 切符号页。
 * ⌫ 删一字符，长按 ≥600ms 清空；密码上限 64 字符。
 */

typedef void (*ui_keyboard_ok_cb_t)(const char *password, void *user_data);
typedef void (*ui_keyboard_cancel_cb_t)(void *user_data);

typedef struct {
    const char *title;              /* 只读标题（目标 SSID） */
    ui_keyboard_ok_cb_t on_ok;      /* 密码非空时回调 */
    ui_keyboard_cancel_cb_t on_cancel;
    void *user_data;
} ui_keyboard_cfg_t;

/**
 * @brief 在 parent_screen 上打开模态键盘（覆盖整屏）
 */
void ui_keyboard_open(lv_obj_t *parent_screen, const ui_keyboard_cfg_t *cfg);

void ui_keyboard_close(void);
bool ui_keyboard_is_open(void);

/* ---- 调试/测试注入口（控制台 KBDMOCK/KBDDUMP 用）---- */
void ui_keyboard_debug_tap(int key_idx);       /* 模拟点击第 key_idx 键（0-11 行优先） */
void ui_keyboard_debug_state(char *out, size_t len); /* 输出缓冲/待定字符/页面状态 */
