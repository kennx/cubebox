# 点阵时钟 + 滑动切换 App 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现点阵数字时钟（app_clock，默认第一屏）、app_ok→app_test 改名（点阵 "OK"）、CST9220 触摸 HAL、以及左右滑动的循环应用导航。

**Architecture:** 三层：`hal_touch`（触摸硬件 + LVGL indev）→ `apps/common/`（dotmatrix 纯逻辑 + LVGL 点阵 widget + swipe_nav 手势导航）→ `app_clock`/`app_test`（生命周期 glue + view）。点阵状态是 23×23 位图（66B），widget 按裁剪区域差量画圆；导航经 `apps_request_open` 交接，复用现有生命周期状态机。

**Tech Stack:** ESP-IDF v5.5.x（esp32c6）、LVGL v8（^8.3.11）、`waveshare/esp_lcd_touch_cst9217` ^1.0.4、主机端 cmake/ctest。

**Spec:** `docs/superpowers/specs/2026-11-10-dotmatrix-clock-swipe-nav-design.md`（已批准；实施时 spec 与本计划同读，冲突处以本计划为准）

## Global Constraints

- 颜色（钉死）：屏背景 `#050505`、灭点 `#151515`（`lv_color_make(0x15,0x15,0x15)`）、亮点纯白。
- 点阵几何（钉死）：23×23；圆半径 8px；首圆心 12px（边距 4px）；圆心 `c(i) = 12 + (i*456 + 11)/22`。
- 触摸参数（官方 07/08/09 示例实证）：I2C 0x5A @400kHz、INT=GPIO 5、RST=GPIO 11、`swap_xy=1, mirror_y=1`；组件名 `waveshare/esp_lcd_touch_cst9217`（spec 曾误写 espressif/，已修正）。
- 触摸失败不致命：`ESP_LOGE` + 跳过 indev 注册 + 继续启动；read 回调读错按"本轮无触摸"。
- 架构铁律：apps 层不 include 驱动头/GPIO；LVGL 仅 gui_task 上下文；生命周期回调内切换应用只许 `apps_request_open`。
- 新增/删除 `main/` 下源文件后必须 `idf.py reconfigure`（GLOB_RECURSE）；构建零错误、零新增警告。
- 编码：ESP-IDF C 风格（K&R、4 空格、snake_case、`#pragma once`）。
- Commit：Conventional Commits（`feat:`/`refactor:`/`docs:` 等），中文 body 按文件分组说明改了什么、为什么。
- 主机测试命令：`cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`。

## Review Focus

1. **未知/小写字符**：`draw_char`/`draw_text` 收到 `'a'` 应映射大写字形，`'$'` 等未知字符按 3 列空白绘制（布局不偏移）→ Task 2 测试步骤覆盖。
2. **越界写**：文本宽于 23 列（起始列为负）、`draw_char` 负坐标/贴右缘 → 逐位裁剪，绝不越界 → Task 2 测试 + tests 全目标开 `-fsanitize=address,undefined`。
3. **触摸初始化失败**：I2C 探测失败/组件返回错误 → 固件继续启动，时钟可走 → Task 5 失败路径代码 + Task 9 上机看启动日志。
4. **手势冒泡**：触摸落在全屏 widget 上，`LV_EVENT_GESTURE` 必须到达 screen（swipe_nav 挂 screen）→ Task 3 给 widget 置 `LV_OBJ_FLAG_GESTURE_BUBBLE` + Task 9 真机滑动验证（spec 写 EVENT_BUBBLE，以 v8 专用的 GESTURE_BUBBLE 为准）。
5. **跨分钟瞬间**：59→00 时数字与冒号同时变化，且冒号每秒翻转 → on_running 无状态全量重算文本（只缓存上次文本做去重），任意跳变自然正确 → Task 6 代码结构固定 + Task 9 上机观察。

---

### Task 1: apps 注册表枚举 API

**Files:**
- Modify: `main/apps/apps.h`（apps_current_name 声明后追加）
- Modify: `main/apps/apps.c`（文件末尾追加实现）
- Test: `tests/test_apps.c`（在"表满"小节**之前**插入，此时已装 a/b/r/min 共 4 个）

**Interfaces:**
- Consumes: 无
- Produces（后续 Task 4、8 依赖）:
  - `size_t apps_count(void);` —— 已安装应用数
  - `const char *apps_name_at(size_t idx);` —— 按安装顺序取应用名；越界返回 NULL

- [ ] **Step 1: 写失败测试**（插入位置：`回切`小节之后、`表满`小节之前）

```c
    /* 枚举 API：安装顺序即下标顺序，越界返回 NULL */
    expect(apps_count() == 4, "count is 4 after a/b/r/min");
    expect_str(apps_name_at(0), "a", "name_at(0) is a");
    expect_str(apps_name_at(3), "min", "name_at(3) is min");
    expect(apps_name_at(4) == NULL, "name_at out of range is NULL");
```

- [ ] **Step 2: 跑测试确认编译失败**

Run: `cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`
Expected: 编译错误 `undefined reference to apps_count/apps_name_at`

- [ ] **Step 3: 实现**（apps.h 加两个声明；apps.c 末尾加实现，count 扫描全表计数，name_at 查 `s_apps[idx]` 为 NULL 则返回 NULL——不假设表紧凑）

- [ ] **Step 4: 跑测试确认通过**

Run: 同 Step 2。Expected: `test_apps: all passed`

- [ ] **Step 5: Commit**

```bash
git add main/apps/apps.h main/apps/apps.c tests/test_apps.c
git commit -m "feat: apps 注册表枚举 API（apps_count/apps_name_at）"
```

---

### Task 2: dotmatrix 纯逻辑模块（位图网格 + 3×5 字体表）

**Files:**
- Create: `main/apps/common/dotmatrix/dotmatrix.h`
- Create: `main/apps/common/dotmatrix/dotmatrix.c`
- Test: `tests/test_dotmatrix.c`（新建）
- Modify: `tests/CMakeLists.txt`（新增 test_dotmatrix 目标；两个目标都加 `-fsanitize=address,undefined`）

**Interfaces:**
- Consumes: 无（纯 C，无 IDF/LVGL）
- Produces（Task 3、6、7 依赖）:

```c
#define DOTMATRIX_COLS 23
#define DOTMATRIX_ROWS 23

typedef struct {
    uint8_t bits[(DOTMATRIX_ROWS * DOTMATRIX_COLS + 7) / 8];
} dotmatrix_grid_t;   /* 66 字节，行主序；外部只经 API 访问位 */

void dotmatrix_clear(dotmatrix_grid_t *g);
bool dotmatrix_get(const dotmatrix_grid_t *g, int row, int col);
void dotmatrix_set(dotmatrix_grid_t *g, int row, int col, bool on);

/* 3×5 字形：uint16_t 共 15 bit，第 r 行第 c 列 → bit(14-3r-c)，bit15 恒 0。
 * 字符集 ' '、':'、'0'-'9'、'A'-'Z'；'a'-'z' 映射大写；未知返回 false。 */
bool dotmatrix_glyph_get(char ch, uint16_t *glyph);

/* 在 (row,col) 为左上角画 3×5 字符：只置位不清位（叠加语义）；
 * 未知字符按 ' '；越界逐位裁剪（row/col 可为负）。 */
void dotmatrix_draw_char(dotmatrix_grid_t *g, int row, int col, char ch);

/* 文本总列数：每字符 3 列 + 1 列间隔 = 4n-1；空串 0 */
int dotmatrix_text_cols(const char *text);

/* 居中绘制：起始行恒 (23-5)/2 = 9，起始列 (23-cols)/2（可为负，逐位裁剪） */
void dotmatrix_draw_text_centered(dotmatrix_grid_t *g, const char *text);
```

**字形表（视觉规格，逐字形钉死；5 组从上到下每行 3 列，`#`=亮 `.`=灭）：**

```
' '  .../.../.../.../...      '0'  ###/#.#/#.#/#.#/###      'D'  ##./#.#/#.#/#.#/##.
'A'  ###/#.#/###/#.#/#.#      '1'  .#./##./.#./.#./###      'E'  ###/#../##./#../###
'B'  ##./#.#/##./#.#/##.      '2'  ###/..#/.#./#../###      'F'  ###/#../##./#../#..
'C'  ###/#../#../#../###      '3'  ###/..#/###/..#/###      'G'  ###/#../#.#/#.#/###
'H'  #.#/#.#/###/#.#/#.#      'I'  ###/.#./.#./.#./###      'J'  ..#/..#/..#/#.#/###
'K'  #.#/#.#/##./#.#/#.#      'L'  #../#../#../#../###      'M'  #.#/###/###/#.#/#.#
'N'  ##./#.#/#.#/#.#/#.#      'O'  ###/#.#/#.#/#.#/###      'P'  ###/#.#/###/#../#..
'Q'  ###/#.#/#.#/###/..#      'R'  ###/#.#/###/##./#.#      'S'  ###/#../###/..#/###
'T'  ###/.#./.#./.#./.#.      'U'  #.#/#.#/#.#/#.#/###      'V'  #.#/#.#/#.#/#.#/.#.
'W'  #.#/#.#/###/###/#.#      'X'  #.#/#.#/.#./#.#/#.#      'Y'  #.#/#.#/.#./.#./.#.
'Z'  ###/..#/.#./#../###      '4'  #.#/#.#/###/..#/..#      '5'  ###/#../###/..#/###
'6'  ###/#../###/#.#/###      '7'  ###/..#/..#/..#/..#      '8'  ###/#.#/###/#.#/###
'9'  ###/#.#/###/..#/###      ':'  .../.#./.../.#./...      （'O' 与 '0' 同形，允许）
```

- [ ] **Step 1: 写失败测试 `tests/test_dotmatrix.c`**

覆盖以下断言（沿用 test_apps.c 的 `expect` 风格，单 `main()` 顺序执行）：
1. 字形表：`' '`、`':'`、`'0'..'9'`、`'A'..'Z'` 全部 `glyph_get==true`；`'a'..'z'` 全 true 且与对应大写字形相等；`'$'`、`'['`、`0x80` 为 false。
2. 位编码：`glyph_get('1')` 后按 bit(14-3r-c) 解码，逐位等于 `.#./##./.#./.#./###`。
3. `text_cols`：`"12:34"`==19、`"OK"`==7、`""`==0、`"A"`==3。
4. set/get/clear 往返：置位后 get true，clear 后全 false（遍历 529 点）。
5. `draw_char('1', 0, 0)`：`get(0,1)`、`get(1,0)`、`get(4,0)` 为 true，`get(0,0)` 为 false。
6. 居中：`draw_text_centered("12:34")` → `get(9,3)` true（'1' 顶行，起始列 2）、冒号点 `get(10,11)` 与 `get(12,11)` true。
7. 呼吸等宽：`"12 34"` 与 `"12:34"` 两张网格逐点比较，恰好差异 2 点：`(10,11)`、`(12,11)`。
8. `"OK"`：`get(9,8)` true（O 顶行起始列 8）、`get(9,13)` false（K 顶行 `#.#` 中列灭，K 起始列 12）。
9. 越界：`draw_char('A', 0, 21)`（右缘，只画列 21、22）与 `draw_char('A', 0, -1)`（列 0、1 可见：`get(0,0)` true）不崩溃、结果正确。
10. 超长：`draw_text_centered("ABCDEFGHIJ")`（40 列 > 23）不崩溃，且 `get(9,0)` true（'C' 落在起始列 0）。
11. 未知字符：`draw_char` 画 `'$'` 与画 `' '` 的两张网格完全相等。

CMakeLists 新目标（并给 `test_apps`/`test_dotmatrix` 两个目标都加 `-fsanitize=address,undefined` 编译+链接选项）：

```cmake
add_executable(test_dotmatrix
    test_dotmatrix.c
    ../main/apps/common/dotmatrix/dotmatrix.c
)
target_include_directories(test_dotmatrix PRIVATE ../main)
target_compile_options(test_dotmatrix PRIVATE -Wall -Werror -fsanitize=address,undefined)
target_link_options(test_dotmatrix PRIVATE -fsanitize=address,undefined)
add_test(NAME test_dotmatrix COMMAND test_dotmatrix)
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`
Expected: 编译失败（dotmatrix.h 不存在）

- [ ] **Step 3: 实现 dotmatrix.{c,h}**

字体表用 `static const uint16_t` 数组 + 查表函数（'0'-'9'、'A'-'Z' 用下标算术，' ' 与 ':' 单独分支；`toupper` 处理小写）。位序：`idx = row*23+col`，字节 `bits[idx/8]` 的 bit `(7 - idx%8)`。

- [ ] **Step 4: 跑测试确认全绿**

Run: 同 Step 2。Expected: `test_apps: all passed` + `test_dotmatrix: all passed`

- [ ] **Step 5: 固件侧验证新增源文件可编译**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告（dotmatrix.c 已被 GLOB 收入固件构建）

- [ ] **Step 6: Commit**

```bash
git add main/apps/common/dotmatrix/dotmatrix.h main/apps/common/dotmatrix/dotmatrix.c tests/test_dotmatrix.c tests/CMakeLists.txt
git commit -m "feat: dotmatrix 纯逻辑模块——23×23 位图网格与 3×5 全字母数字字体表"
```

---

### Task 3: view_dotmatrix LVGL 点阵 widget

**Files:**
- Create: `main/apps/common/dotmatrix/view_dotmatrix.h`
- Create: `main/apps/common/dotmatrix/view_dotmatrix.c`

**Interfaces:**
- Consumes: Task 2 全部 API
- Produces（Task 6、7 依赖）:

```c
#include "lvgl.h"
#include "dotmatrix.h"

typedef struct {
    lv_color_t dot_on;   /* 亮点色 */
    lv_color_t dot_off;  /* 灭点色 */
} dotmatrix_style_t;

#define DOTMATRIX_COLOR_ON  lv_color_white()
#define DOTMATRIX_COLOR_OFF lv_color_make(0x15, 0x15, 0x15)
#define DOTMATRIX_BG_COLOR  lv_color_make(0x05, 0x05, 0x05)  /* 由 app screen 样式使用 */

lv_obj_t *dotmatrix_widget_create(lv_obj_t *parent, const dotmatrix_style_t *style);
void dotmatrix_widget_set_grid(lv_obj_t *widget, const dotmatrix_grid_t *grid);
```

- [ ] **Step 1: 实现 view_dotmatrix.c**

LVGL v8 自定义类模式，要点（算法签名不定处照此实现）：
- 实例结构 `typedef struct { lv_obj_t obj; dotmatrix_grid_t grid; dotmatrix_style_t style; } dotmatrix_widget_t;`（`obj` 必须首成员）。
- 类常量：`.constructor`、`.event_cb`、`.width_def = 480`、`.height_def = 480`、`.instance_size = sizeof(dotmatrix_widget_t)`、`.base_class = &lv_obj_class`。
- 构造：清零网格；`style==NULL` 用默认宏；`lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE)`（后者使手势冒泡到 screen，Review Focus #4）。
- `LV_EVENT_DRAW_MAIN`：`lv_event_get_draw_ctx` 取裁剪区，遍历 529 点，圆域 `{cx-8, cy-8, cx+8, cy+8}` 与裁剪区相交才 `lv_draw_rect`（`radius=8`，`bg_color` 按 `dotmatrix_get` 取 on/off；圆心 `c(i)=12+(i*456+11)/22`）。
- `set_grid`：与内部网格逐位 diff，变化点 invalidate `{cx-9, cy-9, cx+9, cy+9}`（半径+1），再整体拷贝。
- 头文件不暴露内部结构；`lv_obj_t *` 直接作句柄。

- [ ] **Step 2: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告

- [ ] **Step 3: Commit**

```bash
git add main/apps/common/dotmatrix/view_dotmatrix.h main/apps/common/dotmatrix/view_dotmatrix.c
git commit -m "feat: dotmatrix LVGL 自定义 widget——单对象差量画 529 圆点"
```

---

### Task 4: swipe_nav 手势导航组件

**Files:**
- Create: `main/apps/common/swipe_nav/swipe_nav.h`
- Create: `main/apps/common/swipe_nav/swipe_nav.c`

**Interfaces:**
- Consumes: Task 1 `apps_count/apps_name_at`；apps.h 既有 `apps_current_name/apps_request_open`
- Produces（Task 6、7、8 依赖）:

```c
#include "lvgl.h"

/* 给 app screen 挂左右滑动导航：左滑下一个、右滑上一个（循环，顺序=安装顺序） */
void swipe_nav_attach(lv_obj_t *screen);

/* 按安装顺序循环步进：delta=+1 下一个、-1 上一个；无当前应用则打开第 0 个。
 * 内部经 apps_request_open 交接（任意任务可安全调用）。 */
void swipe_nav_step(int delta);
```

- [ ] **Step 1: 实现 swipe_nav.c**

要点：`swipe_nav_attach` 用 `lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_GESTURE, NULL)`；`gesture_cb` 里 `lv_indev_get_gesture_dir(lv_indev_get_act())`，`LV_DIR_LEFT`→`swipe_nav_step(1)`、`LV_DIR_RIGHT`→`swipe_nav_step(-1)`（上下忽略）。`swipe_nav_step`：`apps_current_name()` 在表内定位当前下标（找不到或 NULL 视为 0），目标 `(cur + delta + n) % n`，`apps_request_open(apps_name_at(target))`；`n==0` 直接返回。

- [ ] **Step 2: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告

- [ ] **Step 3: Commit**

```bash
git add main/apps/common/swipe_nav/swipe_nav.h main/apps/common/swipe_nav/swipe_nav.c
git commit -m "feat: swipe_nav 左右滑动手势导航——循环切换走 apps_request_open"
```

---

### Task 5: hal_touch 触摸 HAL

**Files:**
- Create: `main/hal/hal_touch.h`
- Create: `main/hal/hal_touch.c`
- Modify: `main/hal/hal.h`（聚合引入 `hal_touch.h`）
- Modify: `main/idf_component.yml`（依赖列表加一行 `waveshare/esp_lcd_touch_cst9217: ^1.0.4`，保持现有条目不动）

**Interfaces:**
- Consumes: 组合根传入的 i2c 总线句柄
- Produces（Task 8 依赖）:

```c
#pragma once
/* 前向声明（与 hal_power.h 逐字一致），上层经 hal.h 聚合引入不传染驱动头 */
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

/* 初始化 CST9220 触摸（0x5A@400kHz、INT=5、RST=11、swap_xy+mirror_y）并注册
 * LVGL 指针输入设备。失败不致命：ESP_LOGE 后返回，固件继续运行。 */
void hal_touch_init(i2c_master_bus_handle_t bus);
```

- [ ] **Step 1: 加依赖并拉取组件**

`idf_component.yml` 加 `waveshare/esp_lcd_touch_cst9217: ^1.0.4` → Run `idf.py reconfigure`
Expected: `managed_components/waveshare__esp_lcd_touch_cst9217/` 出现

- [ ] **Step 2: 核对已装组件头文件，确定构造 API 形态**

Read `managed_components/waveshare__esp_lcd_touch_cst9217/include/esp_lcd_touch_cst9217.h`。
- 旧式 3 参（`esp_lcd_touch_new_i2c_cst9217(panel_io, &tp_cfg, &tp)`，配套 `esp_lcd_new_panel_io_i2c_v2(bus, &io_config, &io)`，`io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG()` + `scl_speed_hz = 400000`）——上游 GitHub 当前形态，预期命中。
- 若为 4 参新式（bus + `esp_lcd_touch_io_i2c_config_t`），按头文件实际签名适配，参数值不变。
按同一头文件确认取点 API（`esp_lcd_touch_read`+`esp_lcd_touch_get_points` 或等价）。

- [ ] **Step 3: 实现 hal_touch.c**

钉死内容：
- `tp_cfg`：`x_max=480, y_max=480, rst_gpio_num=11, int_gpio_num=5, .levels={.reset=0,.interrupt=0}, .flags={.swap_xy=1,.mirror_y=1}`；组件要求的 `driver_data`（如 `esp_lcd_touch_io_cst9217_config_t{.dev_addr=...}`）按其头文件补齐。
- 失败语义（Review Focus #3）：panel io / touch 构造任一步失败 → `ESP_LOGE`（含 esp_err 名）→ return，不注册 indev，不 abort。
- LVGL indev：`lv_indev_create()` + `lv_indev_set_type(LV_INDEV_TYPE_POINTER)` + `lv_indev_set_read_cb`。
- read_cb 语义：组件取点成功且点数>0 → `data->state = LV_INDEV_STATE_PR`、填 `data->point.x/y`；无点或读失败 → `LV_INDEV_STATE_REL`。句柄存文件内 `static`。

- [ ] **Step 4: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告

- [ ] **Step 5: Commit**

```bash
git add main/hal/hal_touch.h main/hal/hal_touch.c main/hal/hal.h main/idf_component.yml dependencies.lock
git commit -m "feat: hal_touch——CST9220 触摸 HAL 与 LVGL 输入注册（失败不致命）"
```

---

### Task 6: app_clock 点阵时钟

**Files:**
- Create: `main/apps/app_clock/app_clock.h`（`extern const app_lifecycle_t app_clock;`，注释指向 docs/apps/app_clock.md，照 app_ok.h 风格）
- Create: `main/apps/app_clock/app_clock.c`
- Create: `main/apps/app_clock/view/view_clock.h`
- Create: `main/apps/app_clock/view/view_clock.c`
- Create: `docs/apps/app_clock.md`（照 docs/apps/app_ok.md 结构：功能、生命周期表、文件构成、验证）

**Interfaces:**
- Consumes: Task 2 dotmatrix、Task 3 view_dotmatrix、Task 4 swipe_nav
- Produces（Task 8 依赖）: `extern const app_lifecycle_t app_clock;`，`.name = "clock"`

```c
/* view_clock.h */
typedef struct {
    lv_obj_t *screen;        /* 专属屏幕，bg 用 DOTMATRIX_BG_COLOR */
    lv_obj_t *dots;          /* dotmatrix widget */
    lv_obj_t *prev_screen;
    char last_text[8];       /* 上次渲染文本；创建时置 "" */
} view_clock_t;

view_clock_t *view_clock_create(void);   /* 建 screen+widget+swipe_nav_attach，失败 NULL */
void view_clock_destroy(view_clock_t *view);
void view_clock_render(view_clock_t *view, const char *text); /* clear→居中画 text→set_grid→存 last_text */
```

- [ ] **Step 1: 实现 view 与 app**

- `app_clock.c` 的 `on_running` 钉死（Review Focus #5 的无状态重算）：

```c
static void app_clock_on_running(app_t *app)
{
    view_clock_t *view = app->user_data;
    if (view == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char text[6];
    snprintf(text, sizeof(text), "%02d%c%02d", tm.tm_hour,
             (tm.tm_sec % 2 == 0) ? ':' : ' ', tm.tm_min);
    if (strcmp(text, view->last_text) == 0) {
        return; /* 内容未变零刷新 */
    }
    view_clock_render(view, text);
}
```

- `on_open`/`on_close` 照 app_ok 模式（create/destroy 存 user_data）；`on_create` 为 NULL。
- 分配用 `lv_mem_alloc/lv_mem_free`，`#include <time.h>`。

- [ ] **Step 2: 写 docs/apps/app_clock.md**（生命周期表：on_create NULL / on_open view 构建 / on_running 每轮比对文本、变化才重绘 / on_close 销毁；验证：主机 ctest（dotmatrix）+ SNAP 截图对比 example.svg）

- [ ] **Step 3: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告（app_clock 暂未安装，由链接器 GC，合法中间态）

- [ ] **Step 4: Commit**

```bash
git add main/apps/app_clock docs/apps/app_clock.md
git commit -m "feat: app_clock——23×23 点阵数字时钟，冒号呼吸，内容变化才刷新"
```

---

### Task 7: app_ok → app_test 改名 + 点阵 "OK"

**Files:**
- Rename: `git mv main/apps/app_ok/app_ok.c main/apps/app_test/app_test.c`（目录与 4 个文件全部同理：app_ok.h→app_test.h、view/view_ok.{c,h}→view/view_test.{c,h}）
- Modify: 上述 4 文件内容（符号 `app_ok`→`app_test`、TAG、注释；`.name = "ok"`→`"test"`）
- Rewrite: `view_test.c` 改为点阵实现
- Rename+Rewrite: `git mv docs/apps/app_ok.md docs/apps/app_test.md` 后改写
- Modify: `main/main.c`（include、`apps_install(&app_test)`、`apps_request_open(app_test.name)`——最小改动保持构建绿；默认应用仍是 test，Task 8 才切到 clock）

**Interfaces:**
- Consumes: Task 2/3/4 组件
- Produces（Task 8 依赖）: `extern const app_lifecycle_t app_test;`，`.name = "test"`；`view_test_t *view_test_create(void); void view_test_destroy(view_test_t *view);`

- [ ] **Step 1: git mv + 符号重命名 + view_test 重写**

`view_test.c` 钉死：结构照 view_clock（screen + dots + prev_screen，无 last_text）；create：screen 样式 bg `DOTMATRIX_BG_COLOR` → `dotmatrix_widget_create(screen, NULL)` → 网格 `dotmatrix_draw_text_centered(grid, "OK")` → `dotmatrix_widget_set_grid` → `swipe_nav_attach(screen)` → `lv_scr_load`。`view_test_t` 字段：`screen/dots/prev_screen`。on_running 保持 NULL（静态画面）。

- [ ] **Step 2: 改写 docs/apps/app_test.md**（功能=点阵 "OK" 验证屏；文件构成 app_test/view_test；验证=SNAP 截图 + 滑动切换）

- [ ] **Step 3: main.c 最小同步**（app_ok 引用全换 app_test，默认打开 test，构建保持绿）

- [ ] **Step 4: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告、无 app_ok 残留引用（`rg app_ok main/` 仅允许出现在本提交后的历史里，实际应为空）

- [ ] **Step 5: Commit**

```bash
git add -A
git status   /* 确认：改名对(R→A)齐全，无 app_ok 残留路径 */
git commit -m "refactor: app_ok 改名 app_test，画面改为点阵 OK"
```

---

### Task 8: main.c 集成 + 项目文档同步

**Files:**
- Modify: `main/main.c`
- Modify: `AGENTS.md`
- Modify: `main/apps/common/README.md`

**Interfaces:**
- Consumes: Task 5 `hal_touch_init`、Task 6 `app_clock`、Task 7 `app_test`、Task 4 `swipe_nav_step`
- Produces: 最终运行形态

- [ ] **Step 1: main.c 集成（钉死内容）**

- `app_main` 开头（I2C 初始化前）：`setenv("TZ", "CST-8", 1); tzset();`
- `hal_display_init()` 之后：`hal_touch_init(i2c_bus);`
- 安装顺序即导航顺序 + 默认第一屏：

```c
    if (!apps_install(&app_clock)) {
        ESP_LOGE(TAG, "apps_install(app_clock) failed");
    }
    if (!apps_install(&app_test)) {
        ESP_LOGE(TAG, "apps_install(app_test) failed");
    }
    apps_request_open(app_clock.name);
```

- `console_task` 命令分支加（line 缓冲 16B 足够）：

```c
            } else if (strcmp(line, "NEXT") == 0) {
                swipe_nav_step(1); /* 循环切下一个已安装应用（触摸失效时的兜底） */
            }
```

include 增补：`apps/app_clock/app_clock.h`、`apps/app_test/app_test.h`、`apps/common/swipe_nav/swipe_nav.h`。

- [ ] **Step 2: AGENTS.md 同步**

- "屏幕与触摸"节：CST9220 由"当前固件未启用"改为已启用（`hal_touch` + `waveshare/esp_lcd_touch_cst9217`，INT=5/RST=11/swap_xy+mirror_y）。
- 项目地图 `apps/` 树：补 `common/dotmatrix/`、`common/swipe_nav/`、`app_clock/`；`app_ok/` 条目改 `app_test/`；main.c 一行描述补时区与 NEXT。
- "远程调试"命令列表：加 `NEXT`（循环切下一个应用）。

- [ ] **Step 3: common/README.md 同步**

- 约定句"组件只依赖 HAL 与 LVGL，不依赖任何具体 app"放宽为"组件不依赖任何具体 app（可依赖 HAL、LVGL 与 apps 框架）"。
- 现状句"目前暂无共享组件"更新为列出 dotmatrix、swipe_nav。

- [ ] **Step 4: 构建验证**

Run: `idf.py reconfigure && idf.py build`
Expected: 成功、零新增警告

- [ ] **Step 5: Commit**

```bash
git add main/main.c AGENTS.md main/apps/common/README.md
git commit -m "feat: main 集成——时钟默认首屏、触摸初始化、TZ、NEXT 命令"
```

---

### Task 9: 全量验证

**Files:** 无新增（验证任务）

- [ ] **Step 1: 主机测试（干净重建）**

```bash
rm -rf tests/build
cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure
```
Expected: `test_apps`、`test_dotmatrix` 双绿

- [ ] **Step 2: 固件构建**

Run: `idf.py build`
Expected: 成功、零错误、零新增警告

- [ ] **Step 3: 上机验证（设备连 USB 后执行；SNAP 跨冒号翻转出现半亮半灭撕裂属预期，重拍取相位——spec 已裁决）**

1. `tools/flash.sh` 烧录 → 屏幕显示点阵时钟，几何/居中对照 `example.svg`（开机约 08:00 起走）。
2. `SNAP` + `tools/screenshot.py` 连拍两张 ≥1s 间隔：冒号一亮一灭。
3. 真机左滑/右滑：clock↔test 循环切换，日志出现 close/open 生命周期轨迹。
4. `NEXT` 命令复验切换（触摸失效兜底路径）。
5. `REBOOT` 抓启动日志：`hal_touch` 初始化成功日志（或失败但不影响时钟运行的降级日志）。

- [ ] **Step 4: 收尾 commit（如有上机修正）**

```bash
git add -A && git commit -m "fix: 上机验证修正（如有）"
```
