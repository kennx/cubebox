# 低功耗模式（自动降亮度回时钟）设计

日期：2026-11-12　|　状态：待批准

## 意图

设备常亮全亮度运行浪费电。需求：无人交互超时后自动进入低功耗——**降到 20% 亮度、
切回时钟 app、设备持续运行**（非深睡，时钟继续走）；触摸屏幕或按 **KEY 按键**唤醒。

## 需求（用户给定）

- 通用配置新增「自动进入低功耗」：**默认 30 秒**，可选 1 / 5 / 10 分钟、永不（原始描述
  「用不」理解为「永不」）。
- 亮度语义（用户已确认）：**降到 20% 绝对值**；配置亮度 <20% 时取配置值（不让低功耗更亮）。
- 任意 app 超时 → 切回时钟 app；唤醒后停留在时钟，恢复正常亮度。

## 关键发现（代码 / 硬件事实）

- **KEY 按键 = GPIO 10**：官方公开资料未标注，本次从 Waveshare 官方原理图 PDF 解析确认
  （Key1=GPIO9/CHIP_PU=BOOT、Key2=PWRON=电源键、Key3=KEY 用户键=GPIO10）。无外部上拉，
  按下接地 → 固件使能内部上拉、低电平有效。GPIO 10 现无占用。
- LVGL v8 `lv_disp_get_inactive_time()` 直接给出无输入时长（触摸即重置），无需新管线。
- gui_task 10ms 主循环是唯一 LVGL 上下文；跨任务切 app 只能走 `apps_request_open()`
  （gui_task 的 `apps_update()` 消费）；背光副作用走 gui_task（config_store 先例）。
- 配置链路：`config_model`（纯逻辑验证+JSON，主机可测）→ `config_store`（NVS+defer）
  → web 配置器（proto 版本随「加字段+加命令」递增，proto 2 先例）。

## 方案

空闲检测与状态机放在 **gui_task 轮询**（单上下文无锁，10ms 粒度足够）；否决：每 app
各自计时（跨切面重复、违反分层）、FreeRTOS 定时器+中断（过度设计）。

| 模块 | 职责 |
|------|------|
| `hal/hal_button.{c,h}`（新） | GPIO10 输入+内部上拉；`hal_button_pressed_since(prev)` 位图消抖轮询。聚合进 `hal.h` |
| `main/low_power.{c,h}`（新） | 纯逻辑状态机（主机可测）：AWAKE→超时→DIMMED→触摸/KEY→AWAKE；动作枚举由薄包装执行：`apps_request_open("clock")` + `hal_display_set_backlight()` |
| `config_model` | 新字段 `idle_timeout`（u16 秒，白名单 {0,30,60,300,600}，0=永不，默认 30）；GETCFG/SETCFG JSON 同步 |
| `config_store` | NVS key `idle_sec`（u16）；新增 `idle_timeout`/`brightness` 只读 getter 供 low_power 用 |
| `main.c` | gui_task 循环插入 `low_power_update()`（apply_pending 之后，亮度配置变更在低功耗期会被重新压回 dim）；console 新增 `WAKE`（远程注入唤醒，对照 NEXT 兜底先例）+ proto 3 |
| `web/` | 通用页新增「自动低功耗」选择卡片（30 秒/1/5/10 分钟/永不）；`DeviceConfig`/`ConfigPatch`/mock 验证同步 |

唤醒语义：低功耗期间任意触摸活动或 KEY 按下 → 恢复配置亮度；唤醒触摸自然透传 LVGL
（可兼作滑动操作）。清醒期 KEY 按下同样重置空闲计时。

## 非目标

- 不做深睡/关机（PWR 长按仍走 AXP2101 原生路径）；不改各 app 内部逻辑；
  不做低功耗期 UI 指示（仅降亮度）。

## 验证

- `tests/`：config_model `idle_timeout` 验证用例 + low_power 状态机用例（纯逻辑）。
- `idf.py build` 零新增警告；`cd web && npm run build` 通过。
- 真机：flash 后 `NEXT` 切到非时钟 app → 等 30s → 日志 + `SNAP` 截图确认切回时钟并降亮
  （亮度降级由日志确认，SNAP 只反映帧缓冲不反映背光）；`WAKE` 验证恢复亮度；
  KEY/触摸实机唤醒请用户顺手按一下确认。
- 文档同步：`docs/features/web-config.md`（proto 3 + 字段 + WAKE）、
  `docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md`（KEY=GPIO10）。
