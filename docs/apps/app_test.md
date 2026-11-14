# app_test

> 路径:`main/apps/app_test/` · 应用名:`test`

框架验证屏（第二屏）：23×23 点阵背景与 app_clock 同款（#050505/#151515/白），
**A-Z 字体测试卡铺满屏**：3×5 字模每行 6 字符（列 0,4,…,20 恰占满 23 列）、
4 行（行 0,6,12,18 恰占满 23 行）= 24 格；字母 26 个放不下，只写前 24 个
（A-X，6×4 网格），Y/Z 丢弃、若有空格则留空（约定见 view_test.c 注释）。
验证 hal(电源→显示→触摸) + apps 框架 + common 组件（dotmatrix/swipe_nav）全链路。

## 生命周期

| 回调 | 实现 | 说明 |
|------|------|------|
| `on_create` | NULL | 无安装期资源 |
| `on_open` | `app_test_on_open` | 构建 view 并加载屏幕，句柄存 `app->user_data` |
| `on_running` | NULL | 静态画面，无周期逻辑 |
| `on_close` | `app_test_on_close` | 切回前一屏并销毁 view，`user_data` 置 NULL |

生命周期契约见 `main/apps/apps.h`（对照 StackChan `mooncake::AppAbility`）。

## 文件构成

```
app_test/
├── app_test.h       # 对外:extern const app_lifecycle_t app_test
├── app_test.c       # 生命周期回调,只做 glue
└── view/
    ├── view_test.h  # view_test_t(screen/dots/prev_screen)+ create/destroy
    └── view_test.c  # LVGL 界面构建与销毁（点阵一次成图）
```

- **app 与 view 分离**：app_test.c 不直接调用 LVGL，界面细节全部收在 `view/`。
- **依赖组件**：`apps/common/dotmatrix/`、`apps/common/swipe_nav/`。
- **屏幕所有权**：view 自建专属 screen(`lv_obj_create(NULL)`)并挂
  `swipe_nav_attach`；销毁时先 `lv_scr_load(prev_screen)` 再删自己的 screen。

## 验证

- 主机端:`ctest`（点阵字体/布局，见 `tests/test_dotmatrix.c`）
- 上机：右滑（或 `NEXT` 命令）从时钟切至此屏显示 A-X 字体测试卡；`SNAP` +
  `tools/screenshot.py` 抓图比对
