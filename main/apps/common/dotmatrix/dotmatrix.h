#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 点阵纯逻辑：23×23 位图网格 + 3×5 点阵字体，无 LVGL/IDF 依赖（主机可测）。
 * 绘制语义为"叠加置位"：draw_char/draw_text 只点亮不清灭，擦除由 clear 负责。
 */
#define DOTMATRIX_COLS 23
#define DOTMATRIX_ROWS 23

/* 网格（66 字节，行主序）；位序为实现细节，外部只经 get/set 访问 */
typedef struct {
    uint8_t bits[(DOTMATRIX_ROWS * DOTMATRIX_COLS + 7) / 8];
} dotmatrix_grid_t;

void dotmatrix_clear(dotmatrix_grid_t *g);
bool dotmatrix_get(const dotmatrix_grid_t *g, int row, int col);
void dotmatrix_set(dotmatrix_grid_t *g, int row, int col, bool on);

/*
 * 3×5 字形（uint16_t 共 15 bit）：第 r 行第 c 列 → bit(14-3r-c)，bit15 恒 0。
 * 字符集：' '、':'、'0'-'9'、'A'-'Z'；'a'-'z' 映射为大写字形。
 * 未知字符返回 false（调用方按空白处理）。
 */
bool dotmatrix_glyph_get(char ch, uint16_t *glyph);

/*
 * 在 (row, col) 为左上角画 3×5 字符。未知字符按 ' '（3 列空白）；
 * 越界（含负坐标）逐位裁剪，绝不越界写。
 */
void dotmatrix_draw_char(dotmatrix_grid_t *g, int row, int col, char ch);

/* 文本总列数：每字形 3 列 + 1 列间隔 = 4n-1；空串返回 0 */
int dotmatrix_text_cols(const char *text);

/* 水平垂直居中绘制：起始行恒 (23-5)/2 = 9；起始列 (23-cols)/2（可为负，
 * 超宽文本居中截断两侧） */
void dotmatrix_draw_text_centered(dotmatrix_grid_t *g, const char *text);
