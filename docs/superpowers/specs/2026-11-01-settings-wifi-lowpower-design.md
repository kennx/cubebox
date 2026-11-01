# CubeBox 设置页 + WiFi 对时 + 低功耗 设计

日期：2026-11-01
状态：待用户批准

## 意图与动机

CubeBox 目前是纯点阵时钟，RTC 只能靠 USB 连电脑对时（`tools/flash.sh`）。本设计让设备脱离电脑独立完成这些事：

- 屏幕上滑打开设置页：WiFi 对时、亮度调节、电量查看
- WiFi 只在对时这一刻短暂连接，完成即彻底断开并释放内存
- 60 秒无操作自动进入低功耗（亮度降到最低可见，时钟照常走）
- 触摸唤醒只恢复亮度——**绝不主动开启 WiFi/蓝牙**，WiFi 仅在用户从设置页主动发起时连接（用户明确要求）

## 目标

1. 时钟页底部上滑 → 滑入设置页（WiFi、亮度、电量三项）
2. 移除时钟页顶部状态栏（HH:MM 时间、电量图标、BT/WiFi 图标整条删除）
3. WiFi 对时流程：扫描 → 选择网络 → （无保存密码时）九宫格输入 → 连接 → SNTP → 写 PCF85063 → 断开并 deinit
4. 60 秒无触摸进入低功耗：背光降到最低可见亮度，时钟继续每秒走时；触摸退出低功耗并重置计时
5. 手机 T9 风格九宫格密码键盘（3×4，多次点按选字符，符号页，大小写切换），可靠输入 WiFi 密码

## 非目标 / 反目标

- 不做蓝牙、不做常驻 WiFi、不做自动重连、不做 Web 配网
- 不做 light-sleep/deep-sleep（低功耗只降亮度，CPU/外设照常，保持行为可预期；深度睡眠留作后续独立需求）
- 时钟页渲染架构不变（像素拦截保留），不把点阵时钟 LVGL 化
- UI 标签用英文（延续现有英文日期风格，避免引入中文字库的 ~500KB flash 开销）
- 不动 TF 卡、音频、IMU；QSPI/SD 总线切换逻辑不变

## 约束

| 项 | 值 |
|---|---|
| 芯片 | ESP32-C6，512KB HP SRAM，无 PSRAM，RISC-V 单核 160MHz |
| 屏 | 480×480 AMOLED，SH8601/CO5300 QSPI，背光命令 0x51（0-255），无背光 GPIO |
| 触摸 | CST9220（驱动 `waveshare/esp_lcd_touch_cst9217` ^1.0.4，I2C 0x5A，RST=GPIO11，INT=GPIO5） |
| 电源 | AXP2101（0x34），电量百分比寄存器 0xA4 |
| RTC | PCF85063（0x51），由电池保持 |
| 框架 | ESP-IDF v5.5.1，LVGL v8.3.11（`LV_MEM_CUSTOM=y`，LVGL 用系统堆） |
| 总线 | I2C0 共享（SDA=8/SCL=7）；LCD QSPI 与 TF 卡共享数据线（现有规则不动） |
| 代码 | AGENTS.md：单文件 <800 行、模块按子系统拆分、避免一次性抽象 |

## 方案选型

- **A 混合架构（已选）**：时钟页保留像素拦截渲染；设置页用原生 LVGL 控件；`flush_cb` 加页面模式开关（时钟页=拦截叠绘，设置页=纯透传）。改动聚焦，点阵时钟零回归风险。
- B 全 LVGL 化：点阵重写为 Canvas/自定义字体，改动大、布局需重调，放弃。
- C 设置页手绘像素：控件一多工作量爆炸，放弃。

选择理由：像素拦截的功耗代价只发生在时钟页（已有的已知权衡）；设置页只在交互时存在，LVGL 原生控件开发效率和动画质量都是现成的。

## 架构设计

### 模块拆分（main.c 907 行 → 10 个源文件，每文件聚焦一个子系统）

| 文件 | 职责 |
|---|---|
| `main/main.c` | app_main 初始化顺序、i2c 总线创建与句柄分发、gui_task 主循环（时钟走时 + 低功耗检查）、USB time_sync_task（保留） |
| `main/bsp_power.c/.h` | AXP2101 初始化、电量读取、LCD 电源复位（ALDO3 时序） |
| `main/bsp_rtc.c/.h` | PCF85063 init/get/set_datetime、day_of_week、时间字符串刷新 |
| `main/bsp_lcd.c/.h` | SPI/QSPI 面板初始化、LVGL 显示驱动与 flush_cb（含页面模式开关）、背光读写、亮度常量 |
| `main/bsp_touch.c/.h` | cst9217 触摸初始化、LVGL indev 注册、最近触摸时间戳（供低功耗判定） |
| `main/ui_clock.c/.h` | 点阵时钟像素叠绘（font_5x7、日期/时间布局，从 main.c 平移）、时钟页手势捕获（上滑→设置页） |
| `main/ui_settings.c` | 设置主页（WiFi 行/亮度滑条/电量行）、页面导航（下滑返回） |
| `main/ui_wifi.c` | WiFi 子页：扫描列表、连接/同步状态显示、已存网络标识 |
| `main/ui_keyboard.c/.h` | T9 九宫格键盘组件（可复用） |
| `main/app_wifi.c/.h` | WiFi 管理任务：扫描/连接/SNTP/断开全流程、凭据 NVS 存取 |

拆分原则：i2c 总线句柄在 main.c 创建后传给各 bsp 模块（AXP2101/RTC/触摸共享总线）；PMIC 与 LCD 的复位耦合放在 bsp_power 提供 `bsp_power_lcd_reset()`，由 bsp_lcd 调用。

### 页面与导航

- `clock_screen`：黑底，无 LVGL 可见对象；一个全屏透明对象捕获 `LV_EVENT_GESTURE`，检测到上滑（手指向上，LVGL `LV_DIR_TOP`）→ 加载设置页
- `settings_screen`：原生 LVGL 控件，创建一次常驻；下滑手势返回时钟页
- WiFi 子页为第三个 screen；键盘为 WiFi 页面上的模态层
- 切换用 `lv_scr_load_anim(..., 250ms, auto_del=false)`，同时切换 `bsp_lcd_set_flush_mode(CLOCK_OVERLAY / PASSTHROUGH)`
- 设置页打开时 overlay 关闭；回到时钟页恢复 overlay + 每秒全屏 invalidate

### 移除状态栏

删除：STATUSBAR_* 常量、icon_wifi/icon_bluetooth/icon_battery_* 位图、flush_cb 中状态栏渲染块、`s_statusbar_area` 对象、`statusbar_set_wifi_state/ble_state` 接口。叠绘仅保留日期区（5px 网格）与时间区（9px 网格）。WiFi 连接状态改在设置页/WiFi 页显示。

### WiFi 状态机（app_wifi）

```
IDLE → SCANNING → (用户选网) → CONNECTING → SNTP_SYNC → DONE
                     (密码错/超时/失败) ↘ FAILED
DONE/FAILED → 断开 + esp_wifi_stop() + esp_wifi_deinit() + sntp_stop() → IDLE
```

- NVS 开机即初始化（存亮度等设置，不碰无线电）；`esp_netif`/event loop/`esp_wifi_init` 懒初始化：进入 WiFi 页首次操作时才创建，开机不碰无线电（用户硬性要求）
- UI → worker：FreeRTOS 命令队列 `{cmd, ssid[33], pwd[65]}`
- worker → UI：共享状态结构体（volatile 状态枚举 + 结果文本），WiFi 页用 `lv_timer` 每 200ms 轮询刷新（单 GUI 任务，无锁）
- 各阶段超时：连接 15s、SNTP 20s；超时或失败都走断开清理路径
- SNTP：`esp_netif_sntp` 配 aliyun + pool.ntp.org + time.google.com；同步成功后 `TZ=CST-8` 本地时间 → `bsp_rtc_set_datetime`，回读校验并打日志
- 断开清理后释放约 40-50KB 堆（WiFi 运行时占用）

### WiFi 凭据 NVS 存储

- 命名空间 `wifi`，槽位 `cred0`..`cred7`，每条 blob `{char ssid[33]; char pwd[65]}`（NVS key ≤15 字符，不能用 SSID 当 key）
- 连接成功才保存（失败密码不落盘）；按 SSID 去重——同 SSID 再次成功连接只更新密码不占新槽；满 8 条淘汰最旧（槽位带序号）
- 下次选中有保存密码的网络直接连接，跳过键盘；若保存的密码连接失败（`WIFI_REASON_AUTH_FAIL` 等认证错误），删除该凭据并重新弹出键盘

### 九宫格键盘（T9 多次点按）

```
字母页（默认小写，⇧ 切换下一个字母大写）:
[1 .,?]  [2 abc]  [3 def]
[4 ghi]  [5 jkl]  [6 mno]
[7 pqrs] [8 tuv]  [9 wxyz]
[#+=]    [0 ␣]    [⌫]

符号页（[#+=] 再按切回 abc）:
[.] [,] [?]
[!] [-] [_]
[+] :] [@]
[~] [␣] [⌫]
```

- 经典手机九宫格行为：多次点按同一键在键面字符集内循环，**循环含数字本身**（按 `2` 依次出 `a→b→c→2`；按 `1` 出 `.,?!1`）；800ms 超时或按其他键提交当前字符
- 仅用于密码输入（不支持隐藏网络/手动 SSID，2026-11-01 用户确认）：键盘顶部显示目标 SSID 名（只读）+ 密码输入框（已输入字符 + `_` 光标）、OK(连接)/取消按钮
- 密码上限 64 字符，超长拒绝输入并闪烁提示
- ⌫ 删一字符；长按 ≥600ms 清空
- 非 ASCII（中文等）SSID 显示为方块/空白（无中文字库，2026-11-01 用户确认接受），选择与连接按字节透传不受影响

### 低功耗（idle）逻辑

- `bsp_touch` 的 indev 回调在每次触摸时更新 `last_activity_ms`（任何 UI 操作都源于触摸，覆盖完整）
- gui_task 每秒检查：`!low_power && now - last > 60000` → 背光降为 `BRIGHTNESS_LOW`（初始 10/255，宏可调，待真机标定）
- 触摸唤醒：`low_power && 触摸` → 恢复用户设定亮度（不重启任何无线电）；首次触摸只唤醒不触发点击（防误触，实现时用"唤醒事件吞掉本次 press"——若 LVGL 实现复杂则接受误触，标定时定）
- 时钟页照常每秒走时 + 全屏 flush（现行为不变）；WiFi 同步进行中进入低功耗不中断同步（同步完成自然断开清理）

### 亮度设置

- 设置页滑条 10-100%，线性映射 0x51 寄存器 `level = pct * 255 / 100`
- NVS `settings/brightness` 持久化，开机应用（默认 100）
- 低功耗亮度 `BRIGHTNESS_LOW` 与用户亮度独立，不互相覆盖

### 电量信息

- 设置页电量行：百分比数字 + 分段电池图形（LVGL 原生绘制，风格延续点阵），设置页打开期间每 5s 用 `lv_timer` 刷新（AXP2101 0xA4）

### 触摸（bsp_touch）

- 在既有 i2c 总线上 `esp_lcd_touch_new_i2c_cst9217`，`x_max/y_max=480`，RST=11，INT=5
- LVGL indev read 回调轮询 `esp_lcd_touch_get_coordinates`
- 方向 flags（swap_xy/mirror_x/mirror_y）以真机实测为准，实现时先按 Waveshare demo 默认，烧录后校验
- 触摸坐标同时馈给低功耗计时器

### 内存与 flash 预算

- LVGL 双缓冲不变（480×40×2B DMA + 静态 swap 480×60×2B）
- WiFi 运行时 ~50KB 堆（临时，deinit 释放）；512KB SRAM 充裕
- 新增 montserrat 14/16/20/28 字体约 150KB flash；factory 分区 6M 无压力

## 构建与配置变更

- `main/idf_component.yml`：+ `waveshare/esp_lcd_touch_cst9217: ^1.0.4`
- `sdkconfig.defaults`：+ `CONFIG_LV_FONT_MONTSERRAT_14/16/20/28=y`
- `main/CMakeLists.txt`：+ 新源文件
- 变更后按 AGENTS.md 执行 `idf.py fullclean && idf.py build`

## 验证计划

- 我方：`idf.py build` 零错误、无新增警告（CI 门槛）
- 真机（用户执行）：触摸方向正确、上滑开设置页、扫描列表正常、T9 输入密码可连、日志见 SNTP→RTC 写入与断开、60s 变暗、触摸唤醒只恢复亮度

## 用户已定决策

- 2026-11-01：方案 A 混合架构
- 2026-11-01：WiFi 密码存 NVS（记住，下次免输入）
- 2026-11-01：低功耗触摸唤醒只恢复亮度；WiFi/蓝牙绝不自动开启，仅用户从设置页主动发起对时才连接
- 2026-11-01：不支持隐藏网络（无手动 SSID 输入，键盘仅用于密码）
- 2026-11-01：接受中文 SSID 显示为方块/空白，不引入中文字库

## 留给实现的自由度

- 设置页配色与间距细节（黑底白字延续时钟风格）
- 触摸方向 flags 真机标定
- `BRIGHTNESS_LOW` 具体值标定
- SNTP 服务器顺序
- 符号页具体字符集合
