# 经典两端齐平双行纯点阵时钟与日期实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现双行纯点阵时钟。第一行为 4x4 圆点英文日期（格式 `"MON JUN 01 2026"`，15字符），第二行为 8x8 圆点大字时间（HH:MM:SS），两者在物理宽度上完美拉伸对齐（423px），日期在时间的上方。

**Architecture:**
1. 重构并扩充 `main/main.c` 里的 `font_5x7` 库至 39 个字符，补齐 A-Z 完整字模。
2. 将 `char_to_font_idx` 升级为使用线性偏移的高性能转换函数。
3. 重构 `lvgl_flush_cb`，通过 `TIME_OFFSET` 和 `DATE_OFFSET` 划定双行，使用 `date_char_x` 绝对物理坐标表进行日期的等宽拉伸渲染。
4. 重塑 `gui_task` 轮询更新逻辑，每秒格式化 `current_date_str` 为如 `"MON JUN 01 2026"`（全大写）格式。

**Tech Stack:** ESP-IDF v5.5.1, C 语言, LVGL v8.x

---

### Task 1: 扩充完整大写英文字模库 (A-Z) 与映射

**Files:**
*   Modify: `main/main.c:18-50`（扩展 `font_5x7` 库并实现高性能映射）

- [ ] **Step 1: 在 `main.c` 头部将 `font_5x7` 字模库扩充至 39 个字符**
  
  用以下完整的字模重写 `font_5x7`，支持 26 个大写英文字母：
  
  ```c
  // 5x7 点阵字模: 0-9、冒号(:)、减号(-)、空格以及 26 个大写英文字母
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
  ```

- [ ] **Step 2: 升级 `char_to_font_idx` 映射函数为高性能版本**
  
  ```c
  static int char_to_font_idx(char ch)
  {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch == ':') return 10;
      if (ch == '-') return 11;
      if (ch == ' ') return 12;
      if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
      if (ch >= 'A' && ch <= 'Z') return 13 + (ch - 'A');
      return 12; // 默认空格
  }
  ```

---

### Task 2: 重构 `lvgl_flush_cb` 对齐绘制逻辑

**Files:**
*   Modify: `main/main.c:300-420`（重写 `lvgl_flush_cb` 逻辑）

- [ ] **Step 1: 在 `main.c` 中添加对齐物理坐标表与宏定义**
  
  ```c
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
  #define TIME_OFFSET_Y       229    // 时间 Y 轴起始：229px (日期35px + 间距25px = 60px 偏移)

  // 15 个日期字符在 423px 水平框内的起始 X 轴局部坐标表（完美实现两端 423px 对齐）
  static const int date_char_x[15] = {
      0, 29, 57, 85, 114, 142, 170, 199, 227, 255, 284, 312, 340, 369, 398
  };
  ```

- [ ] **Step 2: 重写 `lvgl_flush_cb` 坐标检测，整合日期在上方、时间在下方的等宽渲染**
  
  ```c
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
  ```

---

### Task 3: 重构 `gui_task` 更新循环日期格式化

**Files:**
*   Modify: `main/main.c:530-580`（修改 `gui_task` 中对 `current_date_str` 的格式化）

- [ ] **Step 1: 升级 `gui_task` 对英文全日期的全大写格式化逻辑**
  
  ```c
                  snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d:%02d", hour, min, sec);
                  snprintf(current_date_str, sizeof(current_date_str), "%s %s %02d 20%02d", 
                           weekdays_str[weekday], months_str[month - 1], day, year);
  ```

---

### Task 4: 编译、烧录与部署

- [ ] **Step 1: 运行本地编译校验**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py build`
  期待输出: `Project build complete. ...`

- [ ] **Step 2: 烧录至开发板**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py flash`
  期待输出: `Hard resetting via RTS pin... Done`

- [ ] **Step 3: 暂存并提交代码修改至本地 Git 仓库**
  
  ```bash
  git add main/main.c
  git commit -m "feat: implement double-line aligned matrix clock with upper date and bottom time"
  ```
