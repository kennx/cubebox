# app_clock

> 路径:`main/apps/app_clock/` · 应用名:`clock`

点阵数字时钟（默认第一屏）：23×23 正圆点阵铺满 480×480，背景 #050505、灭点
#151515、亮点白色；3×5 点阵字模 24 小时制 HH:MM 水平垂直居中（视觉基准：
项目根目录 `example.svg`），冒号两点 1 秒亮 1 秒灭呼吸。时间源为系统时钟
（时区 UTC+8，开机约 08:00 起走秒；RTC/SNTP 同步为后续独立工作）。

## 生命周期

| 回调 | 实现 | 说明 |
|------|------|------|
| `on_create` | NULL | 无安装期资源 |
| `on_open` | `app_clock_on_open` | 构建 view 并加载屏幕，句柄存 `app->user_data` |
| `on_running` | `app_clock_on_running` | 无状态重算显示文本（`HH:MM`/`HH MM`），与上次相同则零刷新 |
| `on_close` | `app_clock_on_close` | 切回前一屏并销毁 view，`user_data` 置 NULL |

生命周期契约见 `main/apps/apps.h`（对照 StackChan `mooncake::AppAbility`）。

## 文件构成

```
app_clock/
├── app_clock.h        # 对外:extern const app_lifecycle_t app_clock
├── app_clock.c        # 生命周期回调；on_running 组装文本并去重刷新
└── view/
    ├── view_clock.h   # view_clock_t(screen/dots/prev_screen/last_text)
    └── view_clock.c   # LVGL 界面构建/渲染/销毁
```

- **依赖组件**：`apps/common/dotmatrix/`（纯逻辑网格 + LVGL 点阵 widget）、
  `apps/common/swipe_nav/`（左右滑切换应用）。
- **冒号呼吸**：冒号灭时以等宽空格替换冒号字符——整屏布局不动，两次渲染
  的网格恰好只差冒号 2 个点，widget 差量刷新只重绘这 2 个点。
- **屏幕所有权**：view 自建专属 screen 并挂 `swipe_nav_attach`；销毁时先
  `lv_scr_load(prev_screen)` 再删自己的 screen。

## 验证

- 主机端:`ctest`（点阵字体/布局/呼吸等宽，见 `tests/test_dotmatrix.c`）
- 上机：烧录后屏幕即点阵时钟；`SNAP` + `tools/screenshot.py` 对比
  `example.svg`，两次截图冒号一亮一灭（跨翻转边界的半亮半灭撕裂属预期，
  重拍取相位）；左右滑动切至 app_test。
