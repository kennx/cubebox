# CubeBox 设置页 + WiFi 对时 + 低功耗 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给点阵时钟设备加上触摸交互：上滑设置页（WiFi 对时 / 亮度 / 电量）、移除顶部状态栏、60 秒无操作低功耗、T9 九宫格密码键盘。

**Architecture:** 混合架构——时钟页保留像素拦截渲染（flush_cb 内叠绘点阵），设置页/WiFi 页用原生 LVGL 控件；flush_cb 按页面模式开关切换拦截/透传。WiFi 仅在用户主动操作时懒初始化，SNTP 对时后彻底断开并 deinit。

**Tech Stack:** ESP-IDF v5.5.1（esp32c6）、LVGL v8.3.11、`waveshare/esp_lcd_touch_cst9217 ^1.0.4`、esp_wifi + esp_netif_sntp、NVS。

**Spec:** `docs/superpowers/specs/2026-11-01-settings-wifi-lowpower-design.md`

## Global Constraints

- ESP-IDF v5.5.1，目标 esp32c6，LVGL v8.3.11（`LV_MEM_CUSTOM=y`）
- 所有 LVGL API 调用只允许在 gui_task 上下文（唯一 LVGL 任务）；其他任务经命令队列/状态结构体/`lv_timer` 轮询交接
- WiFi/蓝牙绝不自动开启；`esp_netif`/`esp_wifi` 仅在用户于 WiFi 页主动操作时懒初始化
- 时钟页保留每秒 `lv_obj_invalidate(lv_scr_act())` 全屏 flush（已知权衡）；设置页内避免无意义全屏 invalidate
- DMA 缓冲 `heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`
- 编码风格：K&R、4 空格、函数 `snake_case`、类型 `PascalCase_t`、头文件 `#pragma once`；单文件 <800 行
- 每个任务门槛：`idf.py build` 零错误、无新增警告；涉及 sdkconfig/依赖变更的任务用 `idf.py fullclean && idf.py build`
- 本仓库无主机端测试设施，且 AGENTS.md 规定验证=构建门槛；因此每个任务的验证步骤是构建 + 真机日志/截图检查（设备已连电脑，截图工具见 Task 2），不引入新测试框架（YAGNI）
- 真机操作：烧录用 `tools/flash.sh PORT`（或 `idf.py -p PORT flash`）；串口日志直接读 USB-Serial/JTAG 口；主 console 是 UART0，USB 数据只在辅助 console（time_sync 通道）
- 提交信息用 Conventional Commits；每个任务至少一个 commit

## Review Focus

（无测试框架，以下输入类/故障模式以"任务内验证步骤 + 真机验收清单条目"钉住）

1. **数字+符号混合密码**（如 `home123!`）：T9 循环含数字（按 `2` 出 `a→b→c→2`）且符号页可达 → Task 8 字符表与真机验证（截图核对输入框）
2. **WiFi 密码错误/保存的密码失效**：认证失败要报错、断开清理、删除失效凭据、可立即重试 → Task 7 状态机 FAILED 路径 + Task 9 真机验证
3. **扫描为空/扫描失败**：列表显示空态提示，不卡死不崩溃 → Task 9 空态分支 + 真机验证
4. **SNTP 超时**：20s 超时后断开清理回到 IDLE 可重试，不悬挂 → Task 7 超时路径（真机可拔路由器验证）
5. **低功耗唤醒不误触**：唤醒的首次触摸不上报为点击；唤醒只恢复亮度不碰无线电 → Task 6 吞触摸机制 + 真机验证（日志确认无 WiFi/BT 启动）
6. **中文 SSID 字节透传**：显示方块但选择/连接按字节不受影响 → Task 9 列表渲染用原始字节拷贝 + 真机验证

---

### Task 1: 模块拆分重构（行为不变）

**Files:**
- Create: `main/bsp_power.c`, `main/bsp_power.h`, `main/bsp_rtc.c`, `main/bsp_rtc.h`, `main/bsp_lcd.c`, `main/bsp_lcd.h`, `main/ui_clock.c`, `main/ui_clock.h`
- Modify: `main/main.c`（瘦身为 app_main + gui_task + time_sync_task）, `main/CMakeLists.txt`

**Interfaces:**

bsp_power.h：
```c
void bsp_power_init(i2c_master_bus_handle_t bus);
esp_err_t bsp_power_get_battery_pct(uint8_t *pct);   // 失败时 *pct=99（保留现有回退）
void bsp_power_lcd_reset(void);                       // ALDO3 高-低-高时序
```

bsp_rtc.h：
```c
void bsp_rtc_init(i2c_master_bus_handle_t bus);
esp_err_t bsp_rtc_get_datetime(uint8_t *hour, uint8_t *minute, uint8_t *second,
                               uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year);
esp_err_t bsp_rtc_set_datetime(uint8_t year, uint8_t month, uint8_t day, uint8_t weekday,
                               uint8_t hour, uint8_t minute, uint8_t second);
int bsp_rtc_day_of_week(int y, int m, int d);         // 0=周日，与 PCF85063 编码一致
extern const char *const bsp_rtc_weekdays_str[7];
extern const char *const bsp_rtc_months_str[12];
```

bsp_lcd.h：
```c
void bsp_lcd_init(void);
void bsp_lcd_set_backlight(uint8_t brightness);       // 0-100，保留现有 0x51 命令实现
```

ui_clock.h：
```c
void ui_clock_overlay_apply(const lv_area_t *area, const lv_color_t *src, lv_color_t *dst);
// flush_cb 每 flush 调用一次：在 dst（swap_buf）上按 area 叠绘日期(5px)/时间(9px)点阵，
// src 为 LVGL 原始像素。本任务从 main.c 的 lvgl_flush_cb 像素循环原样平移。
```

- Consumes: 现有 main.c 全部代码（平移源）
- Produces: 上述四个模块；i2c 总线句柄在 main.c 创建后传给 `bsp_power_init`/`bsp_rtc_init`

- [ ] **Step 1: 创建四个模块，把 main.c 代码按职责平移**

规则：纯平移不改逻辑。归属：AXP2101 寄存器操作+PMIC init+电量+LCD 复位→bsp_power；PCF85063+BCD+星期+月字符串→bsp_rtc；SPI/QSPI+panel+LVGL 显示驱动+flush_cb+背光+tick 定时器→bsp_lcd；font_5x7+char_to_font_idx+日期/时间布局常量+current_time_str/current_date_str+叠绘像素循环→ui_clock。flush_cb 主体留 bsp_lcd，像素级叠绘改调 `ui_clock_overlay_apply(area, color_map, swap_buf)`。

- [ ] **Step 2: 更新 `main/CMakeLists.txt` 加入新源文件**

- [ ] **Step 3: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告（对比拆分前）

- [ ] **Step 4: Commit**

```bash
git add main/
git commit -m "refactor: split main.c into bsp_power/bsp_rtc/bsp_lcd/ui_clock modules

无行为变化，按 AGENTS.md 模块化要求把 907 行 main.c 按子系统平移，
i2c 总线句柄由 main.c 创建并注入各 bsp 模块。"
```

---

### Task 2: 截图调试工具（SNAP 命令 + tools/screenshot.py）

设备已连电脑；提供屏幕截图能力供后续所有 UI 任务做真机视觉验证。原理：flush_cb 本就持有送显前最终像素（含点阵叠绘），置位后按条带把 RGB565 hex 流经 USB-Serial/JTAG 辅助 console（与 SETTIME 同通道，UART0 主 console 不受影响）发出。

**Files:**
- Create: `tools/screenshot.py`
- Modify: `main/bsp_lcd.c/.h`（快照状态机 + flush 流式输出）, `main/main.c`（time_sync_task 加 `SNAP` 命令；gui_task 轮询快照请求触发全屏 invalidate）

**Interfaces:**

bsp_lcd.h 增加：
```c
void bsp_lcd_snapshot_arm(void);                       // 任意任务调用：置位快照请求（volatile）
bool bsp_lcd_snapshot_begin_if_requested(void);        // gui_task 每循环调用；true=本次需 lv_obj_invalidate 全屏
// flush_cb 内部：active 时每条带输出 "#SNAP x1 y1 x2 y2\n" + RGB565 hex + 换行；
// 覆盖到 y2==479 的条带后输出 "#SNAP END\n" 并自动失活。额外 invalidate 区域按包头坐标覆盖写，乱序无害。
```

- Consumes: Task 1 的 bsp_lcd flush_cb / swap_buf
- Produces: `tools/screenshot.py`（`python tools/screenshot.py -p PORT -o out.png`，pyserial 收流重组 480×480 RGB565→PNG）；后续任务用它做视觉验证

- [ ] **Step 1: bsp_lcd 快照状态机 + flush 流式输出（`usb_serial_jtag_write_bytes`）**

- [ ] **Step 2: main.c 控制台加 `SNAP` 命令 + gui_task 轮询触发**

- [ ] **Step 3: 写 tools/screenshot.py（解析条带头、hex 解码、Pillow 存 PNG；依赖 pyserial+Pillow，环境缺则 pip 安装）**

- [ ] **Step 4: 构建、烧录、实拍验证**

Run: `idf.py build && tools/flash.sh PORT`（或 `idf.py -p PORT flash`），然后 `python tools/screenshot.py -p PORT -o /tmp/clock.png`
Expected: PNG 生成；打开为 480×480 时钟页（状态栏仍在——本任务在移除状态栏之前）
注意：截图通道是 USB-Serial/JTAG CDC 口（Linux 通常 `/dev/ttyACM*`），烧录前先 `ls /dev/ttyACM*` 确认端口号

- [ ] **Step 5: Commit**

```bash
git add tools/screenshot.py main/
git commit -m "feat: add on-device screenshot tooling via USB console SNAP command

flush_cb 条带流式输出最终送显像素（含点阵叠绘），
host 端 screenshot.py 重组 RGB565 为 PNG，供开发期视觉验证。"
```

---

### Task 3: 触摸驱动 + LVGL indev（bsp_touch）

**Files:**
- Create: `main/bsp_touch.c`, `main/bsp_touch.h`
- Modify: `main/idf_component.yml`（+ `waveshare/esp_lcd_touch_cst9217: ^1.0.4`）, `main/CMakeLists.txt`, `main/main.c`（app_main 里 `bsp_touch_init(i2c_bus)`，在 `bsp_lcd_init` 之后）

**Interfaces:**

```c
void bsp_touch_init(i2c_master_bus_handle_t bus);  // cst9217 + LVGL pointer indev 注册
uint32_t bsp_touch_last_activity_ms(void);         // 最近一次触摸的 tick 毫秒数（供低功耗）
void bsp_touch_set_swallow_once(void);             // 置位：下一次 press 只唤醒不上报给 LVGL
```

- Consumes: main.c 的 i2c 总线句柄；bsp_lcd 已注册的 LVGL 显示
- Produces: 上述 API（Task 5 低功耗依赖 `last_activity_ms`/`swallow_once`）

- [ ] **Step 1: `idf_component.yml` 加依赖，写 bsp_touch.c/.h**

实现要点：`esp_lcd_touch_new_i2c_cst9217(bus, &cfg, &tp)`，`x_max=y_max=480`、`rst_gpio_num=11`、`int_gpio_num=5`、levels 按 Waveshare demo 默认；LVGL v8 `lv_indev_drv_t`（type=LV_INDEV_TYPE_POINTER），read 回调轮询 `esp_lcd_touch_get_coordinates`；**方向 flags（swap_xy/mirror_x/mirror_y）先按 demo 默认，文件顶部集中成 3 个宏便于真机一键翻转**；read 回调里任何 press 都更新 `s_last_activity_ms`；若 `s_swallow_once` 且本周期有 press，则本期报"无触摸"并清标志（吞掉唤醒点击）。

- [ ] **Step 2: 构建验证（依赖变更→fullclean）**

Run: `idf.py fullclean && idf.py build`
Expected: 组件下载成功、零错误、无新增警告

- [ ] **Step 3: Commit**

```bash
git add main/idf_component.yml main/bsp_touch.c main/bsp_touch.h main/CMakeLists.txt main/main.c
git commit -m "feat: add CST9220 touch input via esp_lcd_touch_cst9217 + LVGL indev

原厂同款组件；方向 flags 集中为宏待真机标定；
indev 回调记录最近触摸时间戳并支持低功耗唤醒吞点击。"
```

---

### Task 4: 移除状态栏 + flush 模式开关

**Files:**
- Modify: `main/ui_clock.c/.h`（删状态栏渲染与位图）, `main/bsp_lcd.c/.h`（加模式开关）, `main/main.c`（删 s_statusbar_area、statusbar_set_wifi_state/ble_state 及相关声明）

**Interfaces:**

bsp_lcd.h 增加：
```c
typedef enum {
    BSP_LCD_FLUSH_CLOCK_OVERLAY,   // 时钟页：叠绘点阵
    BSP_LCD_FLUSH_PASSTHROUGH,     // 设置页：纯透传
} bsp_lcd_flush_mode_t;
void bsp_lcd_set_flush_mode(bsp_lcd_flush_mode_t mode);
bsp_lcd_flush_mode_t bsp_lcd_get_flush_mode(void);
```

- Consumes: Task 1 的 ui_clock 叠绘
- Produces: 模式开关（Task 5 页面导航调用）；overlay 仅剩日期区+时间区

- [ ] **Step 1: 删除状态栏代码**

删：STATUSBAR_* 全部宏、icon_wifi/icon_bluetooth/icon_battery_shell/icon_battery_cell、flush 叠绘中状态栏 if 分支、`s_statusbar_area` 创建与样式、`statusbar_set_wifi_state`/`statusbar_set_ble_state`/`s_wifi_connected`/`s_ble_active`、main.c 顶部两个原型声明。`s_battery_pct` 保留（电量行与低功耗仍用，由 `bsp_power_get_battery_pct` 更新）。

- [ ] **Step 2: bsp_lcd 加模式开关，flush_cb 按 `BSP_LCD_FLUSH_PASSTHROUGH` 跳过 overlay**

- [ ] **Step 3: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告；`rg -n "STATUSBAR|statusbar|icon_wifi|icon_bluetooth|icon_battery" main/` 无残留（icon_battery_* 若 Task 4 电量图形复用点阵则允许保留在 ui_clock——实现时决定，二选一即可）

- [ ] **Step 4: Commit**

```bash
git add main/
git commit -m "feat: remove clock-page statusbar and add flush mode switch

顶部 HH:MM+电量+图标整条移除（设计决策：WiFi 状态改在设置页显示）；
flush_cb 支持时钟叠绘/纯透传两种模式，为设置页铺路。"
```

---

### Task 5: 设置主页 + 页面导航 + 亮度持久化 + 电量行

**Files:**
- Create: `main/ui_settings.c`, `main/ui_settings.h`
- Modify: `main/ui_clock.c/.h`（时钟页手势对象 + `ui_clock_show()`）, `main/bsp_lcd.c/.h`（亮度百分比 API）, `main/sdkconfig.defaults`（+ montserrat 14/16/20/28）, `main/main.c`（app_main 加 `nvs_flash_init`（含 NO_FREE_PAGES 擦除重试）、`ui_settings_create()`、gui_task 调 `ui_clock_tick_refresh()`）

**Interfaces:**

bsp_lcd.h 增加：
```c
void bsp_lcd_set_brightness_pct(uint8_t pct);   // 10-100 合法；写 0x51 + NVS
uint8_t bsp_lcd_get_brightness_pct(void);
#define BSP_LCD_BRIGHTNESS_LOW_PCT 4            // ≈10/255，待真机标定
```

ui_clock.h 增加：
```c
void ui_clock_init(void);   // 创建 clock_screen（黑底+全屏透明手势对象）
void ui_clock_show(void);   // 滑回时钟页 + flush 切 CLOCK_OVERLAY
```

ui_settings.h：
```c
void ui_settings_create(void);   // app_main 末尾调用，创建常驻 settings_screen
void ui_settings_show(void);     // 滑入设置页 + flush 切 PASSTHROUGH
```

- Consumes: Task 3 indev、Task 4 模式开关、Task 2 截图工具（视觉验证）
- Produces: 页面导航框架（Task 9 WiFi 页复用"子页 screen + 下滑返回"模式）；NVS 命名空间约定 `settings`/`brightness`（uint8_t，默认 100）

- [ ] **Step 1: sdkconfig.defaults 开字体，写 ui_settings（三行布局）**

设置页结构：标题 `Settings`；行 1 `WiFi >`（本任务点击仅高亮，Task 9 接 WiFi 页）；行 2 `Brightness` + `lv_slider`（range 10-100，初值 NVS 读取，`LV_EVENT_VALUE_CHANGED` 调 `bsp_lcd_set_brightness_pct`）；行 3 `Battery` + 百分比标签 + 分段电池图形（`lv_obj`+样式绘制，每 5s `lv_timer` 刷新 `bsp_power_get_battery_pct`，仅设置页可见时刷新）。页面级 `LV_EVENT_GESTURE` 下滑（LV_DIR_BOTTOM）→ `ui_clock_show()`。黑底白字延续时钟风格。
亮度持久化放 bsp_lcd 内部（`settings`/`brightness`），`bsp_lcd_init` 末尾读 NVS 应用（默认 100）。

- [ ] **Step 2: ui_clock 手势**

`ui_clock_init`：`lv_obj_t *clock_screen = lv_obj_create(NULL)`，黑底；全屏透明对象（`LV_OBJ_FLAG_CLICKABLE`，透明背景无边界）捕获 `LV_EVENT_GESTURE`，`lv_indev_getgesture_dir`==`LV_DIR_TOP` → `ui_settings_show()`。`lv_scr_load(clock_screen)` 需在 gui_task 首帧前完成（`ui_clock_init` 由 gui_task 开头调用，与现有 `lv_scr_act()` 背景逻辑合并）。gui_task 现有的每秒 RTC 刷新改调 `ui_clock_tick_refresh()`（内部读 bsp_rtc 更新字符串，维持每秒 `lv_obj_invalidate`）。

- [ ] **Step 3: 导航动画与模式切换**

`ui_settings_show`/`ui_clock_show` 互相调用 `lv_scr_load_anim(目标, LV_SCR_LOAD_ANIM_MOVE_TOP/BOTTOM, 250, 0, false)` 并切 flush 模式（anim 开始前切，防止过渡帧叠绘残留）。screen 常驻不自动销毁。

- [ ] **Step 4: 构建验证（sdkconfig 变更→fullclean）**

Run: `idf.py fullclean && idf.py build`
Expected: 零错误、无新增警告

- [ ] **Step 5: Commit**

```bash
git add main/
git commit -m "feat: add settings page with swipe navigation, brightness and battery

时钟页上滑入设置页（250ms 动画+flush 模式切换），下滑返回；
亮度滑条 10-100% 持久化 NVS，电量行每 5s 刷新。"
```

---

### Task 6: 低功耗 idle 管理

**Files:**
- Modify: `main/main.c`（gui_task 低功耗判定）, `main/bsp_touch.h/.c`（若无则补 swallow 实现）

**Interfaces:**
- Consumes: `bsp_touch_last_activity_ms()`、`bsp_touch_set_swallow_once()`、`bsp_lcd_set_brightness_pct()`、`BSP_LCD_BRIGHTNESS_LOW_PCT`
- Produces: main.c 内静态 `s_low_power`（无需对外暴露）

- [ ] **Step 1: gui_task 每秒判定**

进入：`!s_low_power && now - bsp_touch_last_activity_ms() >= 60000` → `bsp_lcd_set_brightness_pct_raw(BSP_LCD_BRIGHTNESS_LOW_PCT)`（**raw 变体不写 NVS、不覆盖用户亮度变量**，bsp_lcd 内区分"用户亮度"与"当前生效亮度"）、`bsp_touch_set_swallow_once()`、`s_low_power=true`。退出：`s_low_power && 触摸活动更新` → 恢复用户亮度（`bsp_lcd_restore_user_brightness()`）、`s_low_power=false`。注意时钟照常每秒走时（不改 tick 路径）；WiFi 同步中不做任何打断（同步自然完成并清理）。
bsp_lcd.h 因此增加：
```c
void bsp_lcd_set_brightness_raw(uint8_t pct);        // 低功耗用：只改当前生效亮度
void bsp_lcd_restore_user_brightness(void);           // 恢复用户设定亮度
```

- [ ] **Step 2: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告

- [ ] **Step 3: Commit**

```bash
git add main/
git commit -m "feat: dim to lowest visible brightness after 60s idle

触摸唤醒仅恢复亮度（首次触摸吞掉防误触），绝不启动 WiFi/BT；
时钟每秒走时不受影响。"
```

---

### Task 7: WiFi 管理器 app_wifi（状态机 + SNTP + 凭据 NVS）

**Files:**
- Create: `main/app_wifi.c`, `main/app_wifi.h`
- Modify: `main/CMakeLists.txt`（REQUIRES 加 esp_wifi、esp_netif_sntp、nvs_flash——若组件依赖已由 IDF 内建则只需 INCLUDES）

**Interfaces:**（见下方完整头文件，Task 8 全部依赖此 API）

```c
typedef enum {
    APP_WIFI_STATE_IDLE = 0,
    APP_WIFI_STATE_SCANNING,
    APP_WIFI_STATE_CONNECTING,
    APP_WIFI_STATE_SYNCING,
    APP_WIFI_STATE_DONE,      // 对时成功，status_text 含 "hh:mm:ss"
    APP_WIFI_STATE_FAILED,    // 失败，status_text 含原因
} app_wifi_state_t;

typedef struct {
    char ssid[33];            // 原始字节（中文 SSID 照存）
    int8_t rssi;
    bool auth_open;           // 开放网络
    bool saved;               // NVS 有凭据
} app_wifi_ap_info_t;

typedef struct {
    app_wifi_state_t state;   // volatile，worker 写 / GUI 读
    char status_text[64];
    app_wifi_ap_info_t ap_list[20];
    size_t ap_count;
} app_wifi_status_t;

void app_wifi_ensure_started(void);                  // 懒初始化 netif+event loop+esp_wifi+worker，幂等
const app_wifi_status_t *app_wifi_get_status(void);  // 返回静态结构体指针
void app_wifi_cmd_scan(void);                        // 入队 SCAN
void app_wifi_cmd_connect(const char *ssid, const char *password); // password==NULL 用 NVS 凭据
bool app_wifi_load_password(const char *ssid, char *pwd_out, size_t len); // UI 判断是否弹键盘
```

- Consumes: `bsp_rtc_set_datetime`/`bsp_rtc_get_datetime`、`bsp_rtc_day_of_week`（Task 1）
- Produces: 上述 API；NVS 凭据格式：命名空间 `wifi`，key `cred0`..`cred7`，blob `{char ssid[33]; char pwd[65];}`，序号字段 `seq`（uint32 递增，满 8 淘汰最小 seq）

- [ ] **Step 1: 实现凭据存取（app_wifi.c 内部函数）**

`creds_save(ssid,pwd)`（成功连接后调；同 SSID 更新不占新槽）、`creds_load(ssid,pwd_out)`、`creds_delete(ssid)`、`creds_has(ssid)`。连接失败且 reason 为认证类（`WIFI_REASON_AUTH_FAIL`/`WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT` 等）时 `creds_delete`。

- [ ] **Step 2: worker 任务状态机**

命令队列 `xQueueReceive`（`{enum cmd; char ssid[33]; char pwd[65];}`）。SCAN：`esp_wifi_scan_start(blocking)` → 按 RSSI 排序去同名 → 填 `ap_list`（每项标 saved）→ IDLE。CONNECT：set config → connect → 等 `IP_EVENT_STA_GOT_IP`（15s 超时）→ SNTP：`esp_netif_sntp_init`（aliyun/pool.ntp.org/time.google.com）→ `esp_netif_sntp_cmd_wait_sync(20000)` → `setenv("TZ","CST-8",1); tzset(); time()+localtime_r()` → `bsp_rtc_set_datetime(年%100,月,日,bsp_rtc_day_of_week(...),时,分,秒)` → 回读校验打日志 `RTC SET OK` → DONE（status_text="Sync OK %02d:%02d:%02d"）。任何失败 → FAILED（status_text 短原因）+ 若认证失败删凭据。DONE/FAILED 之后统一清理：`sntp_stop`+`esp_netif_sntp_deinit`、`esp_wifi_disconnect`、`esp_wifi_stop`、`esp_wifi_deinit`，回 IDLE。**注意 worker 任务常驻但 esp_wifi 资源按需建拆**（deinit 后再次命令需重新 init——`ensure_wifi_stack()` 内部处理）。

- [ ] **Step 3: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告

- [ ] **Step 4: Commit**

```bash
git commit -m "feat: add WiFi manager with SNTP time sync and NVS credentials

懒初始化无线电；对时成功写 PCF85063 并回读校验；
DONE/FAILED 一律断开+stop+deinit 释放 ~50KB 堆。
凭据按 SSID 去重存 NVS，认证失败自动删除失效密码。"
```

---

### Task 8: T9 九宫格键盘 ui_keyboard

**Files:**
- Create: `main/ui_keyboard.c`, `main/ui_keyboard.h`
- Modify: `main/CMakeLists.txt`

**Interfaces:**

```c
typedef void (*ui_keyboard_ok_cb_t)(const char *password, void *user_data);
typedef void (*ui_keyboard_cancel_cb_t)(void *user_data);

typedef struct {
    const char *title;              // 只读显示（目标 SSID）
    ui_keyboard_ok_cb_t on_ok;      // password 非空回调；worker 由调用方处理
    ui_keyboard_cancel_cb_t on_cancel;
    void *user_data;
} ui_keyboard_cfg_t;

void ui_keyboard_open(lv_obj_t *parent_screen, const ui_keyboard_cfg_t *cfg); // 模态覆盖层
void ui_keyboard_close(void);
```

- Consumes: Task 5 的字体与 UI 风格
- Produces: 键盘组件（Task 9 在 WiFi 页调用）

- [ ] **Step 1: 键面数据表与多击状态机**

字母页键序（循环集含数字）：`1→".,?!1"`、`2→"abc2"` … `9→"wxyz9"`、`0→" 0"`；符号页：`[.] [,] [?] [!] [-] [_] [+] [:] [@] [~]` + `[␣]` `[⌫]` 布局同 3×4。状态机：同键连击循环 index++；异键或 800ms `lv_timer` 到期提交当前字符入缓冲；⇧ 键切换下一字母大写（指示灯）；`#+=` 切页。缓冲上限 64，超长闪烁输入框边框（`lv_obj_set_style_border_color` 300ms 后还原）不追加。⌫ 删一字符，长按 ≥600ms（`lv_indev` long press 事件）清空。
UI：模态全屏黑底容器：顶部 title 只读 + 密码框（字符 + `_` 光标）+ 3×4 按钮矩阵（`lv_btnmatrix` 也可自绘 12 个 `lv_obj` 按钮，实现者选一）+ OK/取消按钮。

- [ ] **Step 2: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告

- [ ] **Step 3: Commit**

```bash
git commit -m "feat: add T9 nine-grid password keyboard

经典多击循环含数字（按 2 出 a→b→c→2），800ms 提交；
⇧ 大写切换、符号页、⌫ 长按清空、64 字符上限。"
```

---

### Task 9: WiFi 设置页 ui_wifi

**Files:**
- Create: `main/ui_wifi.c`, `main/ui_wifi.h`
- Modify: `main/ui_settings.c`（WiFi 行点击 → `ui_wifi_show()`）

**Interfaces:**

```c
void ui_wifi_create(void);   // 与 settings/clock screen 同级创建，常驻
void ui_wifi_show(void);     // 滑入 + 打开即自动扫描 + flush PASSTHROUGH
```

- Consumes: `app_wifi_*`（Task 7）、`ui_keyboard_*`（Task 8）、导航模式（Task 5）、截图工具（Task 2）
- Produces: 完整对时用户流程

- [ ] **Step 1: 页面结构与扫描列表**

标题 `WiFi` + 状态行（`status_text` 直显）+ 列表（`lv_obj` 可滚动容器，每网络一行：SSID 原始字节直显（中文=方块可接受）、RSSI 四段信号图形、锁图标=加密、`*`=已存）。进入页自动 `app_wifi_ensure_started()`+`app_wifi_cmd_scan()`；`lv_timer` 200ms 轮询状态：SCANNING 显示转圈文本；列表为空且非扫描中显示 `No networks found`；下拉手势返回设置页。刷新按钮重扫。
点击网络：`auth_open` → 直接 `app_wifi_cmd_connect(ssid, "")`；`app_wifi_load_password` 命中 → `app_wifi_cmd_connect(ssid, NULL)`；否则 `ui_keyboard_open`，on_ok → `app_wifi_cmd_connect(ssid, pwd)`。
连接/同步中状态行显示，DONE 显示 `Sync OK hh:mm:ss` 3 秒后可再操作；FAILED 显示原因并允许重试（再次点击网络）。

- [ ] **Step 2: 构建验证**

Run: `idf.py build`
Expected: 零错误、无新增警告

- [ ] **Step 3: Commit**

```bash
git commit -m "feat: add WiFi settings page with scan list and connect flow

打开自动扫描；保存过密码直连，否则弹 T9 键盘；
对时结果与失败原因实时显示；中文 SSID 字节透传。"
```

---

### Task 10: 收尾——文档、全量构建、真机验收清单

**Files:**
- Modify: `AGENTS.md`（项目地图更新：新模块一览、LVGL 输入说明）, `docs/features/`（新增功能说明 `wifi-time-sync.md`）

**Interfaces:**
- Consumes: Task 1-9 全部
- Produces: 验收材料

- [ ] **Step 1: 更新 AGENTS.md 项目地图与 docs/features/ 功能文档**

- [ ] **Step 2: 全量清理构建（配置+依赖曾变更）**

Run: `idf.py fullclean && idf.py build`
Expected: 零错误、无新增警告；`idf.py size` 确认 factory 分区占用 <6M

- [ ] **Step 3: 真机验收清单（用户执行，结果记录进 docs/features/）**

1. 触摸方向正确（画线测试；若 X/Y 镜像错误，改 `bsp_touch.c` 顶部方向宏重烧）
2. 时钟页顶部无状态栏，点阵时钟/日期正常每秒走时
3. 时钟页底部上滑 250ms 滑入设置页；设置页下滑返回
4. 亮度滑条实时生效，重启后保持
5. T9 键盘输入 `home123!` 类混合密码准确
6. WiFi 扫描列表出现、空态提示正常
7. 错密码连接 → FAILED 原因显示 → 正确密码重试成功；错误密码未被保存
8. 中文 SSID 显示方块但可连接
9. 60s 无操作变暗；触摸唤醒只恢复亮度、时钟未停、无 WiFi/BT 启动日志
10. 对时成功后日志出现 `RTC SET OK` 且时钟立即跳到正确时间；同步完成后日志出现 WiFi deinit

- [ ] **Step 4: Commit**

```bash
git commit -m "docs: update AGENTS.md module map and add wifi-time-sync feature doc"
```
