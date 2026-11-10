# 点阵时钟 + 滑动切换 App 设计

日期：2026-11-10
状态：定稿（builder 审查问题已裁决），待用户批准

## 意图与动机

CubeBox 固件目前只有一个演示应用 app_ok。本次为它建立"多应用 + 触摸导航"的产品形态：

1. 第一屏是点阵数字时钟（视觉基准：项目根目录 `example.svg`）——23×23 正圆点阵、
   3×5 点阵字模、冒号呼吸，作为设备默认应用。
2. 启用触摸硬件（CST9220），左右滑动在已安装应用间循环切换（幻灯片式，一屏一应用）。
   切换即触发生命周期交接：新 app 走 open/running，旧 app 走 close——复用现有
   apps 框架，不引入 app_launcher 应用。
3. app_ok 改名为 app_test，改为点阵背景 + 点阵字母 "OK"，作为框架验证屏。

## 目标

- `app_clock`：24 小时制 HH:MM，冒号两点 1 秒亮 1 秒灭；点阵水平垂直居中。
- `app_test`：app_ok 改名（目录/符号/文档），显示点阵 "OK"。
- `hal_touch`：CST9220（经 `esp_lcd_touch_cst9217` 组件）+ LVGL 输入设备注册。
- 左右滑动循环切换应用，顺序 = 安装顺序（clock → test）。
- 3×5 字体覆盖全部大写英文字母 A-Z、数字 0-9、`:` 与空格。
- 颜色集中定义、经 API 参数可替换（暂用编译期默认值，不加 TODO）。

## 非目标

- RTC（PCF85063）/SNTP 时间同步——时钟先用系统时钟走时，同步后续单独做。
- 滑动跟手动画（两 app 同屏过渡）——与"整屏生命周期交接"模型冲突，明确不做。
- app_launcher 应用（图标网格/列表）——幻灯片导航不需要，已与用户确认。
- 运行时改颜色的 UI/配置文件——只做 API 参数化 + 默认宏。
- 页码指示点、动画特效。

## 反目标（做得再对也算失败）

- 529 个 LVGL 对象（内存与遍历开销）。
- 每秒全屏 `lv_obj_invalidate`。
- apps 层直接操作 GPIO/I2C 或 include 驱动头。

## 约束

- 硬件：ESP32-C6、480×480 AMOLED（SH8601 QSPI）、LVGL v8、CST9220 I2C 0x5A、
  TP_INT = GPIO 5（见 `docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md`；Waveshare
  官方用 `esp_lcd_touch_cst9217` 驱动库，实测可用）。
- 架构：`apps → hal` 单向依赖；LVGL 仅 gui_task 上下文；跨任务切换仅
  `apps_request_open`；纯逻辑模块进 `tests/` 主机测试（对照 apps.c 做法）。
- I2C 总线 0 与 AXP2101 等共享；esp_lcd_touch 接受现成 bus 句柄，无总线冲突
  （触摸读取在 gui_task 的 lv_timer_handler 内，读取耗时微秒级，与电源读取无并发窗口问题）。
- `main/CMakeLists.txt` GLOB_RECURSE 收源码：新增文件后须 `idf.py reconfigure`。

## 方案选择（已与用户确认）

**点阵渲染 A1：自定义 LVGL widget**（否决 529 个 lv_obj 对象方案：>50KB RAM、
刷新遍历慢；否决 canvas：全屏 buffer 460KB 不可行）。
点阵状态是一张 23×23 位图（66 字节）；widget 在 draw 事件里按裁剪区域只画涉及的
圆；时间变化只 invalidate 变化的点。

**滑动识别 B1：LVGL 内建手势**（否决自写 indev 坐标轨迹分析：重复造轮子）。
每个 app 的 screen 挂 `LV_EVENT_GESTURE` 回调，`lv_indev_get_gesture_dir()` 判向：
左滑 → 下一个、右滑 → 上一个（循环），内部 `apps_request_open()` 交接。

## 组件设计

### 1. `apps/common/dotmatrix/` —— 点阵组件（两个使用者：clock、test）

**`dotmatrix.{c,h}` 纯逻辑，无 LVGL/IDF 依赖，进主机测试：**

- 常量：`DOTMATRIX_COLS 23`、`DOTMATRIX_ROWS 23`。
- 网格：`typedef struct { uint8_t bits[(23 * 23 + 7) / 8]; } dotmatrix_grid_t;`（66B，行主序）。
- API：
  - `void dotmatrix_clear(dotmatrix_grid_t *g);`
  - `bool dotmatrix_get(const dotmatrix_grid_t *g, int row, int col);`
  - `void dotmatrix_set(dotmatrix_grid_t *g, int row, int col, bool on);`
  - `bool dotmatrix_glyph_get(char ch, uint16_t *glyph);` —— 查 3×5 字模（15 bit，行主序，
    最高 3 位为第 0 行，从左到右）。
  - `void dotmatrix_draw_char(dotmatrix_grid_t *g, int row, int col, char ch);` —— 在
    (row, col) 为左上角画 3×5 字符，越界部分裁剪不写。
  - `int dotmatrix_text_cols(const char *text);` —— 文本总列数（见布局规则）。
  - `void dotmatrix_draw_text_centered(dotmatrix_grid_t *g, const char *text);` —— 网格中心对齐绘制。
- **字体表**：38 字形（' '、':'、'0'-'9'、'A'-'Z'），每字形 `uint16_t` 常量，合计 76B。
  所有字形统一 3 列宽（冒号 3 列、点在中间列的第 1、3 行——与 example.svg 冒号一致；
  空格 3 列全灭——保证冒号呼吸时用空格替换冒号、布局不变）。
  字形位图采用标准清晰 3×5 风格，示例（# 为亮点）：

  ```
  '0'  ###   '1'  .#.   '2'  ###   '3'  ###   '4'  #.#
       #.#        ##.        ..#        ..#        #.#
       #.#        .#.        .#.        ###        ###
       #.#        .#.        #..        ..#        ..#
       ###        ###        ###        ###        ..#

  ':'  ...   'O'  ###   'K'  #.#
       .#.        #.#        #.#
       ...        #.#        ##.
       .#.        #.#        #.#
       ...        ###        #.#
  ```

  （A-Z 全表在实现时落入 `dotmatrix.c` 并由主机测试覆盖，风格与上表一致。）

  > **设计决策（builder 可不再纠结）**：example.svg 里的数字位图内部不一致
  > （'8' ≠ '0' 加中段：'0' 右列缺行 1/3 像素而 '8' 缺行 3 左像素，疑似 SVG 生成
  > 瑕疵），故不逐位复刻 SVG 数字，采用上表标准字形；冒号位置/间距与 SVG 完全一致。

**`view_dotmatrix.{c,h}` LVGL 自定义 widget（LVGL 依赖，上机验证）：**

- `lv_obj_t *dotmatrix_widget_create(lv_obj_t *parent, const dotmatrix_style_t *style);`
  - `dotmatrix_style_t { lv_color_t dot_on; lv_color_t dot_off; }`；`style == NULL` 用
    默认宏 `DOTMATRIX_COLOR_ON`（白）/ `DOTMATRIX_COLOR_OFF`（#151515）——颜色配置点。
  - 基于 `lv_obj_class` 自定义类（`instance_size` 含网格拷贝 + 颜色），尺寸 480×480。
- `void dotmatrix_widget_set_grid(lv_obj_t *w, const dotmatrix_grid_t *g);`
  - 与内部拷贝逐位 diff，变化点仅 invalidate 该点外接方域，再更新拷贝。
- draw（`LV_EVENT_DRAW_MAIN`）：遍历与 `draw_ctx->clip_area` 相交的行列，每点画
  半径 R 的圆（`lv_draw_rect` + `radius`）：亮点 `dot_on`，灭点 `dot_off`。
- 几何（整数像素，与 SVG 常量差 < 1px/点）：边距 4px、圆半径 8px、圆心间距
  pitch = (480 − 2×(4+8)) / 22，圆心 `c(i) = 12 + round(i × 456 / 22)`。
- 背景透明：#050505 由所在 screen 样式负责，widget 只画点。

### 2. `hal/hal_touch.{c,h}` —— 触摸 HAL

- `hal_touch_init(i2c_master_bus_handle_t bus);` —— 由 main.c 组合根调用
  （句柄前向声明照抄 hal_power.h 模式）；聚合进 `hal.h`。
- 实现：`esp_lcd_touch_new_i2c_cst9217()`（I2C 0x5A、INT GPIO 5、RST GPIO 11、
  400kHz、swap_xy+mirror_y——官方 07/08/09 示例实证参数，组件
  `waveshare/esp_lcd_touch_cst9217` 加入 `idf_component.yml`）→ 创建 `lv_indev`
  （`LV_INDEV_TYPE_POINTER`），read 回调经 `esp_lcd_touch_read()`/`get_points` 填充
  LVGL 点坐标。
- 触摸坐标方向若与显示不一致（Waveshare 文档提示有方向参数），在 read 回调内
  交换/镜像——上机验证点。
- LVGL indev 属于硬件输入设备，在 HAL 注册与 hal_display 注册显示驱动同性质。
- **失败语义（已裁决）**：触摸初始化失败（组件返回错误/I2C 探测失败）**不致命**——
  `ESP_LOGE` + 跳过 indev 注册 + 继续启动，设备仍是一台能走时的时钟，SNAP/NEXT
  兜底照常可用；与 hal_power/hal_display 的 `ESP_ERROR_CHECK` 中止语义不同，理由：
  触摸非生存必需。运行期 read 回调内 I2C 偶发错误按"本轮无触摸"静默处理。

### 3. `apps/common/swipe_nav/` —— 滑动导航组件

- `void swipe_nav_attach(lv_obj_t *screen);` —— 给 screen 挂 `LV_EVENT_GESTURE`
  回调（LVGL 内建手势检测，阈值用 indev 默认值）。
- 回调：`lv_indev_get_gesture_dir(lv_indev_get_act())` → `LV_DIR_LEFT` 下一个 /
  `LV_DIR_RIGHT` 上一个；app 顺序 = apps 注册表安装顺序（循环）：
  新增 apps API `size_t apps_count(void);` / `const char *apps_name_at(size_t idx);`
  （越界返回 NULL），由 `apps_current_name()` 定位当前下标，目标经
  `apps_request_open()` 交接——天然满足"回调内只许 request"的契约。
  组件直接依赖 apps 框架（不做回调注入抽象，已裁决）；据此把 `common/README.md`
  依赖约定从"只依赖 HAL 与 LVGL"放宽为"不依赖任何具体 app（可用 apps 框架）"。
- 仅一个 app 在屏时触摸落在其全屏 widget 上：LVGL v8 gesture 事件发给按压对象，
  需冒泡到 screen——widget 创建时置 `LV_OBJ_FLAG_EVENT_BUBBLE`（view_dotmatrix
  内完成，业务 view 无感）。上机验证点，若路径不通则退化为逐对象挂回调。

### 4. `apps/app_clock/` —— 点阵时钟（默认第一屏）

- `app_clock.{c,h}` 生命周期 + `view/view_clock.{c,h}` UI（目录结构对照 app_ok）。
- `on_open`：`view_clock_create()` —— 建 screen（bg #050505）+ dotmatrix widget +
  `swipe_nav_attach(screen)`；句柄存 user_data。
- `on_running`：`time()` + `localtime_r` 取时分秒；显示内容 = 分钟数 + 冒号状态
  （`tm_sec % 2 == 0` 为亮）。与上次比较，变化才重建网格：冒号亮画 `"HH:MM"`，
  灭画 `"HH MM"`（空格等宽，布局稳定）；`dotmatrix_widget_set_grid()` 做差量刷新。
  on_running 每轮 ~10ms 调用，不变时零开销。
- `on_close`：`view_clock_destroy()`。
- 时区：main.c 里 `setenv("TZ", "CST-8", 1); tzset();`——系统时间未同步时开机
  显示 08:00 起走秒（后续 RTC/SNTP 同步是独立工作）。

### 5. `apps/app_test/` —— app_ok 改名 + 点阵 OK

- `git mv` 目录与文件（app_ok→app_test、view_ok→view_test），符号 `app_ok`→`app_test`，
  应用名 `"ok"`→`"test"`。
- `view_test_create()`：screen（bg #050505）+ dotmatrix widget +
  `dotmatrix_draw_text_centered(grid, "OK")`（7 列宽，居中）+ `swipe_nav_attach`。
- 静态画面，on_running 为 NULL。

### 6. `main.c` 改动

- `setenv/tzset`（见上）。
- `hal_display_init()` 后调用 `hal_touch_init(i2c_bus)`。
- 安装顺序即导航顺序：`apps_install(&app_clock)` → `apps_install(&app_test)`；
  默认启动 `apps_request_open(app_clock.name)`。
- console 增加 `NEXT` 命令：循环切到下一个应用（触摸 bring-up 失败时的兜底验证
  途径，也方便远程测生命周期）。

### 7. tests

- 新增 `tests/test_dotmatrix.c`：字形表 38 个全部可查且未知字符安全；文本列数
  公式（`"12:34"` = 19 列）；居中定位（"12:34" 起始列 2、行 9–13）；冒号呼吸等宽
  （"12 34" 与 "12:34" 仅冒号两点位差异）；draw_char 越界裁剪；clear/set/get。
- `tests/test_apps.c` 扩展 `apps_count` / `apps_name_at` 用例。
- widget/HAL/导航依赖 LVGL 与硬件：上机验证（SNAP 截图 + 真机滑动）。

### 8. 文档

- 新增 `docs/apps/app_clock.md`；`docs/apps/app_ok.md` → `docs/apps/app_test.md` 改写。
- `AGENTS.md`：更新"触摸未启用"表述（CST9220 已启用、组件名）；项目地图补
  common 组件与 app 目录。
- `apps/common/README.md` 现状句更新。

## 验证方式

1. 主机：`cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure`。
2. `idf.py build` 零错误零新增警告（新增源文件后 reconfigure）。
3. 上机：`tools/flash.sh` 烧录 → SNAP 截图对比 example.svg（时钟居中、点阵几何、
   冒号两次截图一亮一灭；截图若跨越冒号翻转边界出现半亮半灭撕裂属预期现象，
   重拍取相位即可——已裁决，不为截图冻结时钟）→ 真机左右滑动验证 clock↔test
   循环切换与生命周期日志 → `NEXT` 命令复验 → `REBOOT` 抓启动日志。

## 风险与应对

| 风险 | 应对 |
|------|------|
| `esp_lcd_touch_cst9217` 与 IDF v5.5.x 兼容性 | 组件管理器拉取验证；不可用则退化为裸 I2C 读 CST9220 寄存器（工作量 +1 天） |
| gesture 事件不到 screen（冒泡路径） | widget 置 EVENT_BUBBLE；仍不通则 swipe_nav 直接挂到 widget |
| 触摸坐标方向与显示不一致 | read 回调内交换/镜像，上机一次校准 |
| 字形细节观感 | 主机测试保证一致性，真机截图微调位图（不影响架构） |

## 工作量预估

约 1.5～2 个工作日的 agent 实施 + 上机联调（触摸 bring-up 是主要不确定项）。
