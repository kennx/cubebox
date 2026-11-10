/*
 * 点阵纯逻辑实现。字体表采用 '#'/'.' 字符串字形（行主序 5 行×3 列），
 * glyph_get 内按统一位序公式转换成 uint16_t——以可读性换 flash（约 +0.5KB），
 * 消除手工十六进制编码的整类笔误风险；调用频率为每秒十次量级，开销可忽略。
 */
#include <ctype.h>
#include <string.h>

#include "apps/common/dotmatrix/dotmatrix.h"

/* 索引：0=' '，1=':'，2..11='0'..'9'，12..37='A'..'Z' */
static const char *const k_font[] = {
    /* ' ' */ "..." "..." "..." "..." "...",
    /* ':' */ "..." ".#." "..." ".#." "...",
    /* '0' */ "###" "#.#" "#.#" "#.#" "###",
    /* '1' */ ".#." "##." ".#." ".#." "###",
    /* '2' */ "###" "..#" ".#." "#.." "###",
    /* '3' */ "###" "..#" "###" "..#" "###",
    /* '4' */ "#.#" "#.#" "###" "..#" "..#",
    /* '5' */ "###" "#.." "###" "..#" "###",
    /* '6' */ "###" "#.." "###" "#.#" "###",
    /* '7' */ "###" "..#" "..#" "..#" "..#",
    /* '8' */ "###" "#.#" "###" "#.#" "###",
    /* '9' */ "###" "#.#" "###" "..#" "###",
    /* 'A' */ "###" "#.#" "###" "#.#" "#.#",
    /* 'B' */ "##." "#.#" "##." "#.#" "##.",
    /* 'C' */ "###" "#.." "#.." "#.." "###",
    /* 'D' */ "##." "#.#" "#.#" "#.#" "##.",
    /* 'E' */ "###" "#.." "##." "#.." "###",
    /* 'F' */ "###" "#.." "##." "#.." "#..",
    /* 'G' */ "###" "#.." "#.#" "#.#" "###",
    /* 'H' */ "#.#" "#.#" "###" "#.#" "#.#",
    /* 'I' */ "###" ".#." ".#." ".#." "###",
    /* 'J' */ "..#" "..#" "..#" "#.#" "###",
    /* 'K' */ "#.#" "#.#" "##." "#.#" "#.#",
    /* 'L' */ "#.." "#.." "#.." "#.." "###",
    /* 'M' */ "#.#" "###" "###" "#.#" "#.#",
    /* 'N' */ "##." "#.#" "#.#" "#.#" "#.#",
    /* 'O' */ "###" "#.#" "#.#" "#.#" "###",
    /* 'P' */ "###" "#.#" "###" "#.." "#..",
    /* 'Q' */ "###" "#.#" "#.#" "###" "..#",
    /* 'R' */ "###" "#.#" "###" "##." "#.#",
    /* 'S' */ "###" "#.." "###" "..#" "###",
    /* 'T' */ "###" ".#." ".#." ".#." ".#.",
    /* 'U' */ "#.#" "#.#" "#.#" "#.#" "###",
    /* 'V' */ "#.#" "#.#" "#.#" "#.#" ".#.",
    /* 'W' */ "#.#" "#.#" "###" "###" "#.#",
    /* 'X' */ "#.#" "#.#" ".#." "#.#" "#.#",
    /* 'Y' */ "#.#" "#.#" ".#." ".#." ".#.",
    /* 'Z' */ "###" "..#" ".#." "#.." "###",
};

static int font_index(char ch)
{
    if (ch == ' ') {
        return 0;
    }
    if (ch == ':') {
        return 1;
    }
    if (ch >= '0' && ch <= '9') {
        return 2 + (ch - '0');
    }
    ch = (char)toupper((unsigned char)ch); /* 小写映射大写 */
    if (ch >= 'A' && ch <= 'Z') {
        return 12 + (ch - 'A');
    }
    return -1;
}

bool dotmatrix_glyph_get(char ch, uint16_t *glyph)
{
    if (glyph == NULL) {
        return false;
    }
    int idx = font_index(ch);
    if (idx < 0) {
        return false;
    }
    uint16_t bits = 0;
    const char *art = k_font[idx];
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 3; c++) {
            if (art[r * 3 + c] == '#') {
                bits |= (uint16_t)(1u << (14 - 3 * r - c));
            }
        }
    }
    *glyph = bits;
    return true;
}

static bool in_bounds(int row, int col)
{
    return row >= 0 && row < DOTMATRIX_ROWS && col >= 0 && col < DOTMATRIX_COLS;
}

bool dotmatrix_get(const dotmatrix_grid_t *g, int row, int col)
{
    if (g == NULL || !in_bounds(row, col)) {
        return false;
    }
    int idx = row * DOTMATRIX_COLS + col;
    return (g->bits[idx / 8] >> (7 - idx % 8)) & 1;
}

void dotmatrix_set(dotmatrix_grid_t *g, int row, int col, bool on)
{
    if (g == NULL || !in_bounds(row, col)) {
        return; /* 越界静默裁剪 */
    }
    int idx = row * DOTMATRIX_COLS + col;
    uint8_t mask = (uint8_t)(1 << (7 - idx % 8));
    if (on) {
        g->bits[idx / 8] |= mask;
    } else {
        g->bits[idx / 8] &= (uint8_t)~mask;
    }
}

void dotmatrix_clear(dotmatrix_grid_t *g)
{
    if (g != NULL) {
        memset(g->bits, 0, sizeof(g->bits));
    }
}

void dotmatrix_draw_char(dotmatrix_grid_t *g, int row, int col, char ch)
{
    if (g == NULL) {
        return;
    }
    uint16_t glyph;
    if (!dotmatrix_glyph_get(ch, &glyph)) {
        glyph = 0; /* 未知字符按 3 列空白，保持布局稳定 */
    }
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 3; c++) {
            if ((glyph >> (14 - 3 * r - c)) & 1) {
                dotmatrix_set(g, row + r, col + c, true);
            }
        }
    }
}

int dotmatrix_text_cols(const char *text)
{
    if (text == NULL) {
        return 0;
    }
    size_t n = strlen(text);
    if (n == 0) {
        return 0;
    }
    return (int)(4 * n - 1); /* 每字形 3 列 + 1 间隔，末字符无间隔 */
}

void dotmatrix_draw_text_centered(dotmatrix_grid_t *g, const char *text)
{
    if (g == NULL || text == NULL) {
        return;
    }
    int start_col = (DOTMATRIX_COLS - dotmatrix_text_cols(text)) / 2;
    int start_row = (DOTMATRIX_ROWS - 5) / 2; /* 恒 9 */
    for (const char *p = text; *p != '\0'; p++) {
        dotmatrix_draw_char(g, start_row, start_col, *p);
        start_col += 4; /* 3 宽 + 1 间隔 */
    }
}
