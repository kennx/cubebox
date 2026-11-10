# app_ok

> 路径:`main/apps/app_ok/` · 应用名:`ok`

全链路演示应用:黑底白字居中「OK」(montserrat 48),验证 HAL(电源→显示)与应用框架的完整通路。设备正常工作时屏幕即显示此画面。

## 生命周期

| 回调 | 实现 | 说明 |
|------|------|------|
| `on_create` | NULL | 无安装期资源 |
| `on_open` | `app_ok_on_open` | 构建 view 并加载屏幕,句柄存 `app->user_data` |
| `on_running` | NULL | 静态画面,无周期逻辑 |
| `on_close` | `app_ok_on_close` | 切回前一屏并销毁 view,`user_data` 置 NULL |

生命周期契约见 `main/apps/apps.h`(对照 StackChan `mooncake::AppAbility`)。

## 文件构成

```
app_ok/
├── app_ok.h       # 对外:extern const app_lifecycle_t app_ok
├── app_ok.c       # 生命周期回调,只做 glue
└── view/
    ├── view_ok.h  # view_ok_t(screen/label/prev_screen)+ create/destroy
    └── view_ok.c  # LVGL 界面构建与销毁
```

- **app 与 view 分离**:app_ok.c 不直接调用 LVGL,界面细节全部收在 `view/`。
- **屏幕所有权**:view 自建专属 screen(`lv_obj_create(NULL)`),记录打开前的
  `lv_scr_act()` 为 `prev_screen`;销毁时先 `lv_scr_load(prev_screen)` 再删
  自己的 screen——避免删除活动屏幕。
- **分配**:view 状态用 `lv_mem_alloc`/`lv_mem_free`,与 LVGL 对象同一分配器,
  仅在 gui_task 上下文调用。

## 验证

- 主机端:`ctest`(框架行为,见 `tests/test_apps.c`)
- 上机:烧录后屏幕显示「OK」;`SNAP` + `tools/screenshot.py` 抓图比对
