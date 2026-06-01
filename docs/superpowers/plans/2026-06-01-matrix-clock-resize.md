# Nothing Phone 风格点阵时钟尺寸与居中优化实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 基于 480x480 的圆形屏幕空间，将 Nothing Phone 风格水平点阵时钟的圆点放大为 8x8 像素，间距设为 1 像素，并引入绝对像素级偏移量实现左右对称和正中心居中对齐。

**Architecture:** 重构 `main/main.c` 中的 `lvgl_flush_cb` 冲刷函数，使用全新的网格像素映射公式，移除所有硬编码的偏置数值。

**Tech Stack:** ESP-IDF v5.5.1, C 语言, LVGL v8.x

---

### Task 1: 重构 `main/main.c` 点阵时钟绘制逻辑

**Files:**
*   Modify: `main/main.c:298-383`（修改 `lvgl_flush_cb` 中的宏定义与格点映射算法）

- [ ] **Step 1: 在 `main/main.c` 中添加和修改常量定义**
  
  修改原本没有抽离出来的网格尺寸、圆点大小、字符大小等硬编码，添加全局宏：
  
  ```c
  // 5x7 点阵时钟网格与尺寸常量定义
  #define GRID_SIZE     9      // 每个网格占 9x9 像素
  #define CLOCK_COLS    47     // 时钟总共占 47 列网格
  #define CLOCK_ROWS    7      // 时钟总共占 7 行网格
  #define OFFSET_X      28     // X 轴偏移：(480 - 47 * 9) / 2 = 28.5 -> 28px
  #define OFFSET_Y      208    // Y 轴偏移：(480 - 7 * 9) / 2 = 208.5 -> 208px
  ```

- [ ] **Step 2: 重写 `lvgl_flush_cb` 中的格点映射算法**
  
  将 `lvgl_flush_cb` 中的映射公式重构为像素偏移局部坐标映射。具体代码为：
  
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

              int local_x = global_x - OFFSET_X;
              int local_y = global_y - OFFSET_Y;

              if (local_x >= 0 && local_x < CLOCK_COLS * GRID_SIZE &&
                  local_y >= 0 && local_y < CLOCK_ROWS * GRID_SIZE) {
                  
                  int grid_x = local_x / GRID_SIZE;
                  int grid_y = local_y / GRID_SIZE;
                  int lx = local_x % GRID_SIZE;
                  int ly = local_y % GRID_SIZE;

                  // 方案 B：8x8 圆点，1px 缝隙间距 (lx < 8 且 ly < 8 时为圆点)
                  if (lx < 8 && ly < 8) {
                      bool is_lit = false;
                      int char_idx = grid_x / 6;
                      int char_col = grid_x % 6;

                      if (char_col < 5) {
                          char ch = current_time_str[char_idx];
                          int font_idx = -1;
                          if (ch >= '0' && ch <= '9') {
                              font_idx = ch - '0';
                          } else if (ch == ':') {
                              font_idx = 10;
                          }

                          if (font_idx >= 0) {
                              int font_row = grid_y;
                              is_lit = (font_5x7[font_idx][font_row] >> (4 - char_col)) & 1;
                          }
                      }

                      if (is_lit) {
                          swap_buf[i] = lv_color_make(229, 229, 229); // 发光淡灰 `#E5E5E5`
                      } else {
                          swap_buf[i] = lv_color_black(); // 灭灯纯黑
                      }
                  } else {
                      swap_buf[i] = lv_color_black(); // 1px 细线纯黑间距
                  }
              } else {
                  swap_buf[i] = lv_color_black(); // 时钟局限区外的区域为黑色
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

- [ ] **Step 3: 检查编译，确保无语法错误和警告**
  
  运行编译命令并确认成功。
  
  运行: `idf.py build`
  期待输出: `Project build complete. ...`

- [ ] **Step 4: 暂存并提交代码修改**
  
  ```bash
  git add main/main.c
  git commit -m "feat: resize dot clock to 8x8 dots with 9x9 grids and absolute pixel alignment"
  ```
