# Nothing Phone 风格点阵时钟与现代英文日期集成实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 8x8 圆点大点阵时间与 Montserrat-22 矢量英文日期标签的优雅融合，并重构 RTC 驱动支持完整年月日与星期读取，最后编译并烧录至开发板。

**Architecture:** 
1. 重构 `main/main.c` 里的 `lvgl_flush_cb` 冲刷逻辑为像素通道透传架构。
2. 扩展 I2C 通信实现突发读取 7 字节 RTC 数据获取完整日期。
3. 在 `gui_task` 中使用 `lv_label` 配合 Montserrat-22 字体显示英文日期并动态更新。

**Tech Stack:** ESP-IDF v5.5.1, C 语言, LVGL v8.x

---

### Task 1: 扩展 RTC 驱动与更新通道透传架构

**Files:**
*   Modify: `main/main.c:134-220`（添加 `rtc_pcf85063_get_datetime` 及其常数定义）
*   Modify: `main/main.c:300-385`（重构 `lvgl_flush_cb`）

- [ ] **Step 1: 在 `main.c` 中添加英文星期/月份数组并实现 7 字节突发读取函数**
  
  在 RTC 驱动段添加常量和全新函数：
  
  ```c
  static const char *weekdays_str[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *months_str[12] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };

  static esp_err_t rtc_pcf85063_get_datetime(uint8_t *hour, uint8_t *minute, uint8_t *second,
                                             uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year)
  {
      if (pcf85063_dev == NULL) {
          return ESP_ERR_INVALID_STATE;
      }
      uint8_t reg_addr = 0x04; /* Seconds register address */
      uint8_t data[7] = {0};
      
      /* 从 0x04 开始突发读取 7 个字节（Seconds, Minutes, Hours, Days, Weekdays, Months, Years） */
      esp_err_t ret = i2c_master_transmit_receive(pcf85063_dev, &reg_addr, 1, data, 7, pdMS_TO_TICKS(1000));
      if (ret != ESP_OK) {
          return ret;
      }
      
      *second  = bcd2dec(data[0] & 0x7F);
      *minute  = bcd2dec(data[1] & 0x7F);
      *hour    = bcd2dec(data[2] & 0x3F);
      *day     = bcd2dec(data[3] & 0x3F);
      *weekday = bcd2dec(data[4] & 0x07);
      *month   = bcd2dec(data[5] & 0x1F);
      *year    = bcd2dec(data[6]);
      
      return ESP_OK;
  }
  ```

- [ ] **Step 2: 重构 `lvgl_flush_cb` 以实现像素通道透传**
  
  修改原本强制全部涂黑的逻辑，改为只对大字点阵发光部位强制点亮/熄灭，其余区域透传 `color_map[i]` 的原始色彩流：
  
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
                      swap_buf[i] = color_map[i]; // 1px 细线间距直接透传底层原始像素（通常背景为黑色）
                  }
              } else {
                  swap_buf[i] = color_map[i]; // 时钟局限区外的区域直接透传 LVGL 渲染像素（以正常显示矢量英文日期）
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

### Task 2: 集成矢量英文日期标签控件

**Files:**
*   Modify: `main/main.c:500-535`（重构 `gui_task` 初始化与数据轮询更新循环）

- [ ] **Step 1: 在 `gui_task` 初始化段创建英文日期矢量 Label**
  
  ```c
  static void gui_task(void *arg)
  {
      /* Set screen background to black */
      lv_obj_t *scr = lv_scr_act();
      lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

      // 创建精美的英文日期矢量 Label
      lv_obj_t *date_label = lv_label_create(scr);
      lv_obj_set_style_text_color(date_label, lv_color_make(229, 229, 229), 0);
      lv_obj_set_style_text_font(date_label, &lv_font_montserrat_22, 0);
      lv_obj_align(date_label, LV_ALIGN_CENTER, 0, -60); // 纵向往上偏移 60 像素，完美排在时间上方
      lv_label_set_text(date_label, "Mon Jun 01 2026"); // 初始化预填

      uint32_t last_rtc_update = 0;
      uint8_t hour = 0, min = 0, sec = 0;
      uint8_t day = 1, weekday = 1, month = 6, year = 26;
      char date_str[32] = "Mon Jun 01 2026";
  ```

- [ ] **Step 2: 重构 `gui_task` 更新循环，调用 `rtc_pcf85063_get_datetime` 并同步更新标签**
  
  ```c
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
                  snprintf(date_str, sizeof(date_str), "%s %s %02d 20%02d", 
                           weekdays_str[weekday], months_str[month - 1], day, year);
                  
                  lv_label_set_text(date_label, date_str);
                  
                  ESP_LOGI(TAG, "Updated display: %s | %s", date_str, current_time_str);
                  
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

### Task 3: 编译与烧录部署

- [ ] **Step 1: 进行本地编译校验**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py build`
  期待输出: `Project build complete. ...`

- [ ] **Step 2: 烧录至开发板**
  
  运行: `. /Users/kenn/.espressif/v5.5.1/esp-idf/export.sh && idf.py flash`
  期待输出: `Hard resetting via RTS pin... Done`

- [ ] **Step 3: 暂存并提交代码修改**
  
  ```bash
  git add main/main.c
  git commit -m "feat: integrate Montserrat-22 modern vector date label with dot clock via rendering transparency pipeline"
  ```
