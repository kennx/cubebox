# 经典双行纯点阵时钟集成实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现双行纯点阵时钟。第一行为 8x8 圆点大字时间（HH:MM:SS），第二行为 4x4 圆点紧凑英文日期（MM-DD WWW），完全用手写点阵在 `lvgl_flush_cb` 冲刷时绘制，彻底移除 LVGL 的矢量英文 Label 控件及 12 号字体的相关修改。

**Architecture:**
1. 重构并扩充 `main/main.c` 里的 `font_5x7` 点阵字模库，支持大写英文字母和常用符号。
2. 实现字符转换点阵索引函数 `char_to_font_idx`。
3. 重构 `lvgl_flush_cb`，通过 `TIME_OFFSET` 和 `DATE_OFFSET` 划定双行互不重叠渲染矩形，进行独立的网格和圆点大小映射渲染。
4. 清理 `gui_task`，删除矢量 `date_label` 相关创建及样式，增加 `current_date_str` 每秒格式化更新逻辑。

**Tech Stack:** ESP-IDF v5.5.1, C 语言, LVGL v8.x

---

### Task 1: 扩充字模库与实现字符映射

**Files:**
*   Modify: `main/main.c:18-36`（扩展 `font_5x7` 数组定义）
*   Modify: `main/main.c:160-220`（添加 `char_to_font_idx`）

- [ ] **Step 1: 在 `main.c` 头部将 `font_5x7` 字模库扩充至 26 个字符，删除 Montserrat-12 声明**
  
  删除 `LV_FONT_DECLARE(lv_font_montserrat_12);` 声明。
  同时，在 `main.c` 头部用以下完整的字模重写 `font_5x7`：
  
  ```c
  // 5x7 点阵字模: 0-9、冒号(:)、减号(-)、空格以及 13 个大写英文字母
  static const uint8_t font_5x7[26][7] = {
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0: 00-09 数字
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
      {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // 14: D
      {0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x1F}, // 15: E
      {0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x10}, // 16: F
      {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 17: I
      {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // 18: M
      {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // 19: N
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 20: O
      {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // 21: R
      {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // 22: S
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // 23: T
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 24: U
      {0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x11}  // 25: W
  };
  ```

- [ ] **Step 2: 增加全局日期字符串 `current_date_str` 定义，并实现 `char_to_font_idx` 映射函数**
  
  在主程序驱动区域上方增加如下定义：
  
  ```c
  static char current_time_str[16] = "00:00:00";
  static char current_date_str[16] = "06-01 MON"; // 全局日期字符串

  static int char_to_font_idx(char ch)
  {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch == ':') return 10;
      if (ch == '-') return 11;
      if (ch == ' ') return 12;
      if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
      switch (ch) {
          case 'A': return 13;
          case 'D': return 14;
          case 'E': return 15;
          case 'F': return 16;
          case 'I': return 17;
          case 'M': return 18;
          case 'N': return 19;
          case 'O': return 20;
          case 'R': return 21;
          case 'S': return 22;
          case 'T': return 23;
          case 'U': return 24;
          case 'W': return 25;
          default:  return 12; // 默认空格
      }
  }
  ```

---

### Task 2: 重构 `lvgl_flush_cb` 双行绘制逻辑

**Files:**
*   Modify: `main/main.c:300-410`（重写 `lvgl_flush_cb` 内的宏和双行几何映射）

- [ ] **Step 1: 在 `main.c` 中添加和修改常量定义**
  
  替换原本的单行时钟宏，替换为以下双行绝对偏移常数定义：
  
  ```c
  // 5x7 点阵时钟与日期（双行点阵）常量定义
  #define TIME_GRID_SIZE      9      // 时间网格 9x9px
  #define TIME_COLS           47     // 时间总列数 47
  #define TIME_ROWS           7      // 时间总行数 7
  #define TIME_OFFSET_X       28     // (480 - 423) / 2 = 28px
  #define TIME_OFFSET_Y       170    // 时间 Y 轴起始：170px

  #define DATE_GRID_SIZE      6      // 日期网格 6x6px
  #define DATE_COLS           59     // 日期总列数 59 ("MM-DD WWW" 共 10 字符)
  #define DATE_ROWS           7      // 日期总行数 7
  #define DATE_OFFSET_X       63     // (480 - 354) / 2 = 63px
  #define DATE_OFFSET_Y       258    // 日期 Y 轴起始：258px
  ```

- [ ] **Step 2: 重写 `lvgl_flush_cb`，拦截双行目标框并做双重网格转换**
  
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

              int tx = global_x - TIME_OFFSET_X;
              int ty = global_y - TIME_OFFSET_Y;

              int dx = global_x - DATE_OFFSET_X;
              int dy = global_y - DATE_OFFSET_Y;

              if (tx >= 0 && tx < TIME_COLS * TIME_GRID_SIZE &&
                  ty >= 0 && ty < TIME_ROWS * TIME_GRID_SIZE) {
                  
                  // 1. 渲染第一行：大点阵时间 (HH:MM:SS)
                  int grid_x = tx / TIME_GRID_SIZE;
                  int grid_y = ty / TIME_GRID_SIZE;
                  int lx = tx % TIME_GRID_SIZE;
                  int ly = ty % TIME_GRID_SIZE;

                  // 8x8 圆点，1px 缝隙间距 (lx < 8 且 ly < 8 时为圆点)
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
              else if (dx >= 0 && dx < DATE_COLS * DATE_GRID_SIZE &&
                       dy >= 0 && dy < DATE_ROWS * DATE_GRID_SIZE) {
                       
                  // 2. 渲染第二行：小点阵日期 (MM-DD WWW)
                  int grid_x = dx / DATE_GRID_SIZE;
                  int grid_y = dy / DATE_GRID_SIZE;
                  int lx = dx % DATE_GRID_SIZE;
                  int ly = dy % DATE_GRID_SIZE;

                  // 6x6 网格，4x4 圆点，2px 缝隙间距 (lx < 4 且 ly < 4 为圆点)
                  if (lx < 4 && ly < 4) {
                      bool is_lit = false;
                      int char_idx = grid_x / 6;
                      int char_col = grid_x % 6;

                      if (char_col < 5) {
                          char ch = current_date_str[char_idx];
                          int font_idx = char_to_font_idx(ch);
                          if (font_idx >= 0) {
                              is_lit = (font_5x7[font_idx][grid_y] >> (4 - char_col)) & 1;
                          }
                      }
                      swap_buf[i] = is_lit ? lv_color_make(229, 229, 229) : lv_color_black();
                  } else {
                      swap_buf[i] = color_map[i]; // 2px 细线间距透传
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

### Task 3: 清理与重构 `gui_task` 轮询逻辑

**Files:**
*   Modify: `main/main.c:515-565`（重构 `gui_task` 逻辑）

- [ ] **Step 1: 删除 `gui_task` 中对 `date_label` 的创建及相关修改，恢复纯黑背景设置，并加入 `current_date_str` 每秒更新逻辑**
  
  ```c
  static void gui_task(void *arg)
  {
      /* Set screen background to black */
      lv_obj_t *scr = lv_scr_act();
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

      uint32_t last_rtc_update = 0;
      uint8_t hour = 0, min = 0, sec = 0;
      uint8_t day = 1, weekday = 1, month = 6, year = 26;

      while (1) {
          uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
          
          /* Update RTC time and date every 1000ms */
          if (now - last_rtc_update >= 1000 || last_rtc_update == 0) {
              if (rtc_pcf85063_get_datetime(&hour, &min, &sec, &day, &weekday, &month, &year) == ESP_OK) {
                  // 越界安全防护
                  if (weekday > 6) weekday = 1;
                  if (month < 1 || month > 12) month = 6;
                  if (day < 1 || day > 31) day = 1;
                  if (year > 99) year = 26;

                  snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d:%02d", hour, min, sec);
                  snprintf(current_date_str, sizeof(current_date_str), "%02d-%02d %s", 
                           month, day, weekdays_str[weekday]);
                  
                  ESP_LOGI(TAG, "Updated display: %s | %s", current_date_str, current_time_str);
                  
                  // Force full screen redraw in LVGL to flush using our global grid rendering
                  lv_obj_invalidate(lv_scr_act());
              } else {
                  ESP_LOGE(TAG, "Failed to read RTC for display update");
              }
              last_rtc_update = now;
          }

          lv_timer_handler();
          vTaskDelay(pdMS_TO_TICKS(10));
      }
  }
  ```

---

### Task 4: 编译与烧录部署

- [ ] **Step 1: 进行本地编译校验**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py build`
  期待输出: `Project build complete. ...`

- [ ] **Step 2: 烧录至开发板**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py flash`
  期待输出: `Hard resetting via RTS pin... Done`

- [ ] **Step 3: 暂存并提交代码修改至本地 Git 仓库**
  
  ```bash
  git add main/main.c
  git commit -m "feat: implement double line full matrix clock showing dot-time and smaller dot-date"
  ```
