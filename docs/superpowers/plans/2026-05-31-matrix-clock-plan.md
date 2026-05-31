# Nothing Phone 风格水平点阵时钟实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将项目当前简单的文本时钟替换为一个极致还原 Nothing Phone 点阵美学、水平分布的时分秒数字时钟，并包含贴合屏幕边缘的 60 秒环形点阵秒针，实现干净整洁的跳秒更新。

**Architecture:** 在 `main/main.c` 现有的 LVGL GUI 任务中重构核心 UI。使用自定义的 5x7 二进制点阵字模实时驱动 8 个字符的微型圆点对象（`lv_obj`）的亮暗状态；使用三角函数在半径 225 像素的圆周上分布 60 个圆形像素点充当秒环，每秒无动画过渡地精准更新。

**Tech Stack:** ESP-IDF v6.0.1, C 语言, LVGL 8.x 图形库, PCF85063 RTC I2C 驱动。

---

### Task 1: 定义点阵字模数据及渲染常量

**Files:**
- Modify: `main/main.c` (在文件顶部合适位置加入字模数据及像素几何参数定义)

- [ ] **Step 1: 写入 5x7 像素字模与宏定义**
  在 `main/main.c` 的 LVGL 引入后（例如在第 16 行 `LV_FONT_DECLARE(lv_font_montserrat_48);` 附近），定义点阵相关的尺寸常量和 11 个字符（0-9 和冒号）的 5x7 二进制字模。

  写入以下 C 代码：
  ```c
  #define DOT_SIZE      6
  #define DOT_GAP       2
  #define CHAR_GAP      10
  #define RING_RADIUS   225
  #define RING_DOTS     60

  // 5x7 点阵字模: 0-9 以及 冒号(:)
  static const uint8_t font_5x7[11][7] = {
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0: 01110
      {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1: 00100
      {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
      {0x1F, 0x01, 0x02, 0x0E, 0x01, 0x11, 0x0E}, // 3
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
      {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
      {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
      {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}, // 9
      {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}  // : (冒号)
  };
  ```

- [ ] **Step 2: 编译验证**
  在终端中执行编译以确保没有引入语法或宏定义冲突。
  Run: `idf.py build`
  Expected: 编译成功（无新增警告或错误）。

- [ ] **Step 3: 提交代码**
  ```bash
  git add main/main.c
  git commit -m "feat: define 5x7 dot matrix font and layout constants"
  ```

---

### Task 2: 构建 8 字符点阵圆点网格 UI 结构

**Files:**
- Modify: `main/main.c` (在 `gui_task` 中替换原有的 UI 初始化)

- [ ] **Step 1: 声明全局或静态的 LVGL 像素对象容器**
  需要为 8 个字符中的每个字符声明 5x7 = 35 个 `lv_obj_t*` 像素指针。
  在 `main/main.c` 的 `gui_task` 内部：
  ```c
  static lv_obj_t *dot_objs[8][5][7]; // [字符索引][列X][行Y]
  ```

- [ ] **Step 2: 替换原本的 `time_label` 初始化逻辑，动态创建 8 字符圆点网格**
  在 `gui_task` 中，定位到创建 `time_label` 的代码（约 377 行到 390 行），将其完整替换为以下创建点阵容器和 35*8=280 个圆形像素点的 C 代码：
  
  ```c
  /* 创建点阵容器并使其水平居中 */
  lv_obj_t *time_container = lv_obj_create(scr);
  lv_obj_set_size(time_container, 374, 54);
  /* 移除容器背景、边框、内边距，使其完全透明 */
  lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(time_container, 0, 0);
  lv_obj_set_style_pad_all(time_container, 0, 0);
  lv_obj_align(time_container, LV_ALIGN_CENTER, 0, 0); // 完美居中
  
  /* 批量创建 8 个字符的点阵网格 */
  for (int c = 0; c < 8; c++) {
      lv_obj_t *char_container = lv_obj_create(time_container);
      lv_obj_set_size(char_container, 38, 54);
      lv_obj_set_style_bg_opa(char_container, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(char_container, 0, 0);
      lv_obj_set_style_pad_all(char_container, 0, 0);
      /* 设置水平排列 X 轴偏移 */
      lv_obj_set_pos(char_container, c * (38 + CHAR_GAP), 0);
      
      for (int x = 0; x < 5; x++) {
          for (int y = 0; y < 7; y++) {
              lv_obj_t *dot = lv_obj_create(char_container);
              lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
              /* 设置像素点网格内相对坐标 */
              lv_obj_set_pos(dot, x * (DOT_SIZE + DOT_GAP), y * (DOT_SIZE + DOT_GAP));
              /* 样式使像素点为圆形，并默认亮起暗灰色 */
              lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
              lv_obj_set_style_border_width(dot, 0, 0);
              lv_obj_set_style_bg_color(dot, lv_color_make(26, 26, 26), 0); // 暗色底 `#1A1A1A`
              lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
              
              dot_objs[c][x][y] = dot;
          }
      }
  }
  ```

- [ ] **Step 3: 编译验证**
  Run: `idf.py build`
  Expected: 编译成功。

- [ ] **Step 4: 提交代码**
  ```bash
  git add main/main.c
  git commit -m "feat: construct 8-character dot grid objects in gui_task"
  ```

---

### Task 3: 绘制边缘 60 点精细秒环

**Files:**
- Modify: `main/main.c` (在 `gui_task` 初始化部分增加秒环构建逻辑)

- [ ] **Step 1: 构建 60 秒极坐标圆形矩阵点阵**
  在 `gui_task` 内部的 `time_container` 后面，加入利用三角函数计算坐标的圆点生成代码。
  
  ```c
  #include <math.h> // 确保 main.c 顶部有引入 math.h
  
  static lv_obj_t *ring_dots[RING_DOTS];
  
  for (int i = 0; i < RING_DOTS; i++) {
      /* 每一个点对应极坐标角度，以正上方为 0 秒 (theta = -90度) */
      float theta = (i * 6.0f - 90.0f) * M_PI / 180.0f;
      int x_pos = (int)(240.0f + RING_RADIUS * cosf(theta)) - 2; // 圆点半径为2 (大小4像素)，做中心补偿
      int y_pos = (int)(240.0f + RING_RADIUS * sinf(theta)) - 2;
      
      lv_obj_t *ring_dot = lv_obj_create(scr);
      lv_obj_set_size(ring_dot, 4, 4);
      lv_obj_set_pos(ring_dot, x_pos, y_pos);
      
      lv_obj_set_style_radius(ring_dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(ring_dot, 0, 0);
      lv_obj_set_style_bg_color(ring_dot, lv_color_make(38, 38, 38), 0); // 边缘秒环默认底色 `#262626`
      lv_obj_set_style_bg_opa(ring_dot, LV_OPA_COVER, 0);
      
      ring_dots[i] = ring_dot;
  }
  ```
  *(注：需要在 `main.c` 顶部引入 `<math.h>`。)*

- [ ] **Step 2: 在 `main.c` 顶部补全 `<math.h>`**
  在 `main.c` 第 1 行附近，确保引入 `#include <math.h>`。

- [ ] **Step 3: 编译验证**
  Run: `idf.py build`
  Expected: 编译成功。

- [ ] **Step 4: 提交代码**
  ```bash
  git add main/main.c
  git commit -m "feat: generate 60 concentric ring dot matrices with polar coordinates"
  ```

---

### Task 4: 实现跳秒更新与字模状态逻辑

**Files:**
- Modify: `main/main.c` (在 `gui_task` 的 `while(1)` 每秒更新时，替换原 `lv_label_set_text` 逻辑)

- [ ] **Step 1: 替换原有 `lv_label_set_text` 的每秒刷新逻辑**
  定位到 `gui_task` 轮询循环中更新时间的代码段（约 395 到 409 行），将读取完 `hour, min, sec` 后的逻辑完整重写，使用字模渲染 8 个数字，并点亮秒针。

  ```c
  if (rtc_pcf85063_get_time(&hour, &min, &sec) == ESP_OK) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, sec);
      ESP_LOGI(TAG, "Updated display: %s", buf);
      
      /* 1. 刷新 8 个点阵字符 */
      for (int c = 0; c < 8; c++) {
          char ch = buf[c];
          int font_idx = 0;
          if (ch >= '0' && ch <= '9') {
              font_idx = ch - '0';
          } else if (ch == ':') {
              font_idx = 10;
          } else {
              continue;
          }
          
          /* 读取该字模，并将对应的点亮起或熄灭 */
          for (int x = 0; x < 5; x++) {
              for (int y = 0; y < 7; y++) {
                  // 取出第 y 行的第 4-x 位（左起第一列是 bit 4）
                  bool is_lit = (font_5x7[font_idx][y] >> (4 - x)) & 1;
                  lv_obj_t *dot = dot_objs[c][x][y];
                  if (is_lit) {
                      lv_obj_set_style_bg_color(dot, lv_color_make(229, 229, 229), 0); // 浅浅冷灰 `#E5E5E5`
                  } else {
                      lv_obj_set_style_bg_color(dot, lv_color_make(26, 26, 26), 0);    // 暗底灰 `#1A1A1A`
                  }
              }
          }
      }
      
      /* 2. 刷新秒环红点秒针 */
      static int last_sec = -1;
      if (last_sec != -1 && last_sec < RING_DOTS) {
          // 恢复上一次秒点为普通暗灰圆环色，尺寸 4x4
          lv_obj_set_size(ring_dots[last_sec], 4, 4);
          lv_obj_set_style_bg_color(ring_dots[last_sec], lv_color_make(38, 38, 38), 0);
          
          // 重新微调坐标，补偿 4x4 居中偏移 (圆心位置补偿 225 半径)
          float last_theta = (last_sec * 6.0f - 90.0f) * M_PI / 180.0f;
          int lx = (int)(240.0f + RING_RADIUS * cosf(last_theta)) - 2;
          int ly = (int)(240.0f + RING_RADIUS * sinf(last_theta)) - 2;
          lv_obj_set_pos(ring_dots[last_sec], lx, ly);
      }
      
      if (sec < RING_DOTS) {
          // 将当前秒点设为 Nothing 标志性红点，尺寸变大为 6x6
          lv_obj_set_size(ring_dots[sec], 6, 6);
          lv_obj_set_style_bg_color(ring_dots[sec], lv_color_make(255, 0, 64), 0); // 亮红 `#FF0040`
          
          // 重新微调坐标，补偿 6x6 居中偏移
          float curr_theta = (sec * 6.0f - 90.0f) * M_PI / 180.0f;
          int cx = (int)(240.0f + RING_RADIUS * cosf(curr_theta)) - 3;
          int cy = (int)(240.0f + RING_RADIUS * sinf(curr_theta)) - 3;
          lv_obj_set_pos(ring_dots[sec], cx, cy);
          
          last_sec = sec;
      }
  } else {
      ESP_LOGE(TAG, "Failed to read RTC for display update");
  }
  ```

- [ ] **Step 2: 编译测试**
  Run: `idf.py build`
  Expected: 编译成功。整个 `main.c` 无新增警告。

- [ ] **Step 3: 提交代码**
  ```bash
  git add main/main.c
  git commit -m "feat: complete jumping update logic for matrix characters and second hand ring"
  ```

---

### Task 5: 深度清理与最终构建验证

**Files:**
- Modify: `main/main.c` (移除原本不再使用的 UI 标签变量)

- [ ] **Step 1: 删除不再使用的旧变量声明**
  检查 `gui_task`，原本在第 378 行附近声明的 `lv_obj_t *time_label` 已经不再使用，将其完整删除，保持代码整洁。
  确认没有遗漏的 `time_label` 引用。

- [ ] **Step 2: 重新进行全局完整编译**
  清除旧的编译产物，重新编译整个项目以检验干净环境下的构建正确性。
  Run: `idf.py fullclean && idf.py build`
  Expected: 编译完美通过。

- [ ] **Step 3: 提交清理**
  ```bash
  git add main/main.c
  git commit -m "refactor: clean up unused old clock elements and final build check"
  ```
