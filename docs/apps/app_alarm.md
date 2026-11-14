# app_alarm

> 路径:`main/apps/app_alarm/` · 应用名:`alarm`

闹钟响铃屏（瞬态）：闹钟到点由 `main.c` 的 alarm_service 切入本应用。23×23
点阵三行布局——行 2-6 当前时间 `HH:MM`（与 app_clock 同款冒号呼吸）、行 9-13
`ALARM` 标识 500ms 闪烁吸引注意、行 16-20 `KEY` 提示（按侧边 KEY 停止响铃）。
响铃时长 3 分钟超时或 KEY 按下后由胶水层切回时钟。声音（ES8311 最大音量、
30s 渐入的合成哔哔声）由 `hal_audio` 起/停，本应用只负责显示，不碰音频。

## 生命周期

| 回调 | 实现 | 说明 |
|------|------|------|
| `on_create` | NULL | 无安装期资源 |
| `on_open` | `app_alarm_on_open` | **响铃态**才构建 view；非响铃被滑动轮换/NEXT 误入时 `apps_request_open(app_clock)` 立即退回（不建 UI） |
| `on_running` | `app_alarm_on_running` | 重算时间文本 + `lv_tick_get()/500` 闪烁相位，与上次相同则零刷新 |
| `on_close` | `app_alarm_on_close` | 切回前一屏并销毁 view，`user_data` 置 NULL |

生命周期契约见 `main/apps/apps.h`。响铃态经 `app_alarm_set_ringing(bool)`
告知（gui_task 在 `apps_request_open` 前调用；请求标志模式，对照
`app_radius_request_radius` 先例）。

## 文件构成

```
app_alarm/
├── app_alarm.h        # 对外:extern const app_lifecycle_t app_alarm + set_ringing
├── app_alarm.c        # 生命周期回调；响铃态门控 + 文本/闪烁相位去重刷新
└── view/
    ├── view_alarm.h   # view_alarm_t(screen/dots/prev_screen/last_text/last_blink)
    └── view_alarm.c   # LVGL 界面构建/渲染/销毁（draw_text_at_row 多行布局）
```

- **依赖组件**：`apps/common/dotmatrix/`（多行布局用 `dotmatrix_draw_char`
  自算列；`draw_text_centered` 的行固定 9，只适合单行）。
- **不挂 swipe_nav**：响铃屏不允许滑走——KEY 与 3 分钟超时是唯二出口；
  但安装注册使它进入轮换序，故 on_open 的非响铃退回兜底误入路径。
- **屏幕所有权**：view 自建专属 screen；销毁时先 `lv_scr_load(prev_screen)`
  再删自己的 screen（同 app_clock 约定）。

## 触发链（跨模块，完整规格见 docs/superpowers/specs/2026-11-14-alarm-design.md）

```
gui_task 每轮 alarm_service（main.c）：
  秒级 alarm_sched_check（alarm.c 纯逻辑，配置来自 config_store）
  → 触发：[单次先 config_store_disable_alarm 持久化关闭]
         → hal_audio_alarm_start()（渐入+最大音量）
         → app_alarm_set_ringing(true) + apps_request_open("alarm")
         → lv_disp_trig_activity(NULL)（退出低功耗并阻止响铃中降亮度）
  响铃中：KEY 下降沿或 ≥3 分钟 → hal_audio_alarm_stop() + 切回 clock
```

## 验证

- 主机端：`ctest`（调度判定/去重/渐入包络，见 `tests/test_alarm.c`；本应用
  UI 无主机测试，对照 app_clock 做法）。
- 上机：`ALARM` console 命令强制立即响铃（无需等到设定时刻）——核对响铃屏
  三行布局、闪烁与 `SNAP` 截图；按 KEY 停止并切回时钟；或经网页配置器设
  1 分钟后的闹钟验证定时触发。声音渐入/音量需人耳确认。
