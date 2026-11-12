/*
 * dotmatrix 主机端测试：字体表、位编码、文本布局、居中、越界裁剪。
 * 直接编译 main/apps/common/dotmatrix/dotmatrix.c（纯 C，无 IDF/LVGL 依赖）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/common/dotmatrix/dotmatrix.h"

static const char *const k_expected_font[] = {
    /* '0' */ "###" "#.#" "#.#" "#.#" "###",
    /* '1' */ ".#." "##." ".#." ".#." "###",
    /* '2' */ "###" "..#" "###" "#.." "###",
    /* '3' */ "###" "..#" ".##" "..#" "###",
    /* '4' */ "#.#" "#.#" "###" "..#" "..#",
    /* '5' */ "###" "#.." "###" "..#" "###",
    /* '6' */ "###" "#.." "###" "#.#" "###",
    /* '7' */ "###" "..#" ".#." ".#." ".#.",
    /* '8' */ "###" "#.#" "###" "#.#" "###",
    /* '9' */ "###" "#.#" "###" "..#" "###",
    /* 'A' */ ".#." "#.#" "###" "#.#" "#.#",
    /* 'B' */ "##." "#.#" "##." "#.#" "##.",
    /* 'C' */ ".##" "#.." "#.." "#.." ".##",
    /* 'D' */ "##." "#.#" "#.#" "#.#" "##.",
    /* 'E' */ "###" "#.." "##." "#.." "###",
    /* 'F' */ "###" "#.." "##." "#.." "#..",
    /* 'G' */ ".##" "#.." "#.#" "#.#" ".##",
    /* 'H' */ "#.#" "#.#" "###" "#.#" "#.#",
    /* 'I' */ "###" ".#." ".#." ".#." "###",
    /* 'J' */ "..#" "..#" "..#" "#.#" ".#.",
    /* 'K' */ "#.#" "#.#" "##." "#.#" "#.#",
    /* 'L' */ "#.." "#.." "#.." "#.." "###",
    /* 'M' */ "#.#" "###" "###" "#.#" "#.#",
    /* 'N' */ "###" "#.#" "#.#" "#.#" "#.#",
    /* 'O' */ ".#." "#.#" "#.#" "#.#" ".#.",
    /* 'P' */ "##." "#.#" "#.#" "##." "#..",
    /* 'Q' */ ".#." "#.#" "#.#" "##." ".##",
    /* 'R' */ "##." "#.#" "#.#" "##." "#.#",
    /* 'S' */ ".##" "#.." ".#." "..#" "##.",
    /* 'T' */ "###" ".#." ".#." ".#." ".#.",
    /* 'U' */ "#.#" "#.#" "#.#" "#.#" "###",
    /* 'V' */ "#.#" "#.#" "#.#" "#.#" ".#.",
    /* 'W' */ "#.#" "#.#" "###" "###" "#.#",
    /* 'X' */ "#.#" "#.#" ".#." "#.#" "#.#",
    /* 'Y' */ "#.#" "#.#" ".#." ".#." ".#.",
    /* 'Z' */ "###" "..#" ".#." "#.." "###",
};

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

/* 两张网格的差异点数 */
static int grid_diff_count(const dotmatrix_grid_t *a, const dotmatrix_grid_t *b)
{
    int diff = 0;
    for (int row = 0; row < DOTMATRIX_ROWS; row++) {
        for (int col = 0; col < DOTMATRIX_COLS; col++) {
            if (dotmatrix_get(a, row, col) != dotmatrix_get(b, row, col)) {
                diff++;
            }
        }
    }
    return diff;
}

int main(void)
{
    dotmatrix_grid_t g, g2;
    uint16_t glyph = 0;

    /* 1. 字形表：全字符集可查；小写映射大写；未知字符 false */
    expect(dotmatrix_glyph_get(' ', &glyph) == true, "glyph: space");
    expect(dotmatrix_glyph_get(':', &glyph) == true, "glyph: colon");
    for (char ch = '0'; ch <= '9'; ch++) {
        expect(dotmatrix_glyph_get(ch, &glyph) == true, "glyph: digit");
    }
    for (char ch = 'A'; ch <= 'Z'; ch++) {
        expect(dotmatrix_glyph_get(ch, &glyph) == true, "glyph: uppercase");
    }
    {
        static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int i = 0; chars[i] != '\0'; i++) {
            const char *art = k_expected_font[i];
            uint16_t expected = 0;
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 3; c++) {
                    if (art[r * 3 + c] == '#') {
                        expected |= (uint16_t)(1u << (14 - 3 * r - c));
                    }
                }
            }
            expect(dotmatrix_glyph_get(chars[i], &glyph) == true, "glyph: SVG character available");
            expect(glyph == expected, "glyph: matches SVG 3x5 design");
        }
    }
    for (char ch = 'a'; ch <= 'z'; ch++) {
        uint16_t lower = 0, upper = 0;
        expect(dotmatrix_glyph_get(ch, &lower) == true, "glyph: lowercase");
        dotmatrix_glyph_get((char)(ch - 'a' + 'A'), &upper);
        expect(lower == upper, "glyph: lowercase maps to uppercase");
    }
    expect(dotmatrix_glyph_get('$', &glyph) == false, "glyph: '$' unknown");
    expect(dotmatrix_glyph_get('[', &glyph) == false, "glyph: '[' unknown");
    expect(dotmatrix_glyph_get((char)0x80, &glyph) == false, "glyph: 0x80 unknown");

    /* 2. 位编码：'1' = .#. / ##. / .#. / .#. / ###（bit 14-3r-c） */
    {
        static const char *art_rows[] = {".#.", "##.", ".#.", ".#.", "###"};
        dotmatrix_glyph_get('1', &glyph);
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 3; c++) {
                bool bit = (glyph >> (14 - 3 * r - c)) & 1;
                expect(bit == (art_rows[r][c] == '#'), "glyph bit encode of '1'");
            }
        }
    }

    /* 3. 文本列数：每字符 3 列 + 1 间隔 */
    expect(dotmatrix_text_cols("12:34") == 19, "text_cols 12:34 == 19");
    expect(dotmatrix_text_cols("OK") == 7, "text_cols OK == 7");
    expect(dotmatrix_text_cols("") == 0, "text_cols empty == 0");
    expect(dotmatrix_text_cols("A") == 3, "text_cols single == 3");

    /* 4. set/get/clear 往返 */
    dotmatrix_clear(&g);
    for (int row = 0; row < DOTMATRIX_ROWS; row++) {
        for (int col = 0; col < DOTMATRIX_COLS; col++) {
            expect(dotmatrix_get(&g, row, col) == false, "clear makes all off");
            dotmatrix_set(&g, row, col, true);
            expect(dotmatrix_get(&g, row, col) == true, "set/get roundtrip");
        }
    }
    dotmatrix_clear(&g);

    /* 5. draw_char('1') 左上角定位 */
    dotmatrix_clear(&g);
    dotmatrix_draw_char(&g, 0, 0, '1');
    expect(dotmatrix_get(&g, 0, 1) == true, "draw_char '1' row0 center");
    expect(dotmatrix_get(&g, 1, 0) == true, "draw_char '1' row1 left");
    expect(dotmatrix_get(&g, 4, 0) == true, "draw_char '1' row4 left");
    expect(dotmatrix_get(&g, 0, 0) == false, "draw_char '1' row0 left off");

    /* 6. 居中布局："12:34" 起始列 2、起始行 9；冒号点 (10,11)/(12,11) */
    dotmatrix_clear(&g);
    dotmatrix_draw_text_centered(&g, "12:34");
    expect(dotmatrix_get(&g, 9, 3) == true, "centered '1' top dot at (9,3)");
    expect(dotmatrix_get(&g, 9, 2) == false, "centered '1' left of top dot off");
    expect(dotmatrix_get(&g, 10, 2) == true, "centered '1' row1 left on");
    expect(dotmatrix_get(&g, 9, 6) == true, "centered '2' top-left at (9,6)");
    expect(dotmatrix_get(&g, 10, 11) == true, "colon upper dot (10,11)");
    expect(dotmatrix_get(&g, 12, 11) == true, "colon lower dot (12,11)");
    expect(dotmatrix_get(&g, 11, 11) == false, "colon middle row off");
    expect(dotmatrix_get(&g, 9, 14) == true, "centered '3' top-left at (9,14)");
    expect(dotmatrix_get(&g, 8, 11) == false, "row above text block off");

    /* 7. 冒号呼吸等宽："12 34" 与 "12:34" 恰好差异 2 点 */
    dotmatrix_clear(&g2);
    dotmatrix_draw_text_centered(&g2, "12 34");
    expect(grid_diff_count(&g, &g2) == 2, "colon->space diff is exactly 2 dots");

    /* 8. "OK" 居中：起始列 8；O 顶行中列亮、K 顶行中列灭 */
    dotmatrix_clear(&g);
    dotmatrix_draw_text_centered(&g, "OK");
    expect(dotmatrix_get(&g, 9, 8) == false, "OK: 'O' top-left off");
    expect(dotmatrix_get(&g, 9, 9) == true, "OK: 'O' top center at (9,9)");
    expect(dotmatrix_get(&g, 9, 12) == true, "OK: 'K' top-left at (9,12)");
    expect(dotmatrix_get(&g, 9, 13) == false, "OK: 'K' top center off");

    /* 9. 越界裁剪：右缘与负坐标不越界写、可见部分正确 */
    dotmatrix_clear(&g);
    dotmatrix_draw_char(&g, 0, 21, 'A'); /* 占列 21..23，列 23 裁剪 */
    expect(dotmatrix_get(&g, 0, 21) == false, "clip right: (0,21) off");
    expect(dotmatrix_get(&g, 0, 22) == true, "clip right: (0,22) on");
    expect(dotmatrix_get(&g, 1, 21) == true, "clip right: 'A' row1 left on");
    expect(dotmatrix_get(&g, 1, 22) == false, "clip right: 'A' row1 center off");
    dotmatrix_clear(&g);
    dotmatrix_draw_char(&g, 0, -1, 'A'); /* 占列 -1..1，负列裁剪 */
    expect(dotmatrix_get(&g, 0, 0) == true, "clip left: row0 (0,0) on");
    expect(dotmatrix_get(&g, 1, 0) == false, "clip left: 'A' row1 center off");
    expect(dotmatrix_get(&g, 1, 1) == true, "clip left: 'A' row1 right on");
    dotmatrix_clear(&g);
    dotmatrix_draw_char(&g, -2, 0, 'A'); /* 占行 -2..2，负行裁剪 */
    expect(dotmatrix_get(&g, 0, 0) == true, "clip top: row0 visible on");

    /* 10. 超长文本（40 列 > 23）：不崩溃，居中截断后 'C' 起始列 0 */
    dotmatrix_clear(&g);
    dotmatrix_draw_text_centered(&g, "ABCDEFGHIJ");
    expect(dotmatrix_get(&g, 9, 0) == false, "overflow: 'C' top-left off");
    expect(dotmatrix_get(&g, 9, 1) == true, "overflow: 'C' top center at (9,1)");
    expect(dotmatrix_get(&g, 9, 3) == false, "overflow: gap column off");
    expect(dotmatrix_get(&g, 9, 21) == false, "overflow: 'H' center off");

    /* 11. 未知字符按 3 列空白：与画 ' ' 的网格完全一致 */
    dotmatrix_clear(&g);
    dotmatrix_draw_char(&g, 0, 0, '$');
    dotmatrix_clear(&g2);
    dotmatrix_draw_char(&g2, 0, 0, ' ');
    expect(grid_diff_count(&g, &g2) == 0, "unknown char draws as blank");

    printf("test_dotmatrix: all passed\n");
    return 0;
}
