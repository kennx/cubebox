# app_radius — 屏幕圆角测量工具

> **研究用临时应用**：测量 480×480 AMOLED 四角物理圆角遮罩的半径。结论沉淀
> 到点阵布局适配后，本应用可整体删除（含 main.c 的注册与 `RAD` 命令）。

## 背景

屏幕四角被黑色边框圆角遮盖（物理盖板，SNAP 截图不可见——framebuffer 圆角区域
像素正常写入，仅被遮挡）。要点阵 UI 完美贴合圆角，需先测出半径。

测试方法（三页图案，触摸点按循环切换）：

| 页 | 内容 | 用途 |
|----|------|------|
| 0 | 全白屏 | 正对屏幕拍照 → `tools/measure_radius.py` 拟合四角半径（主方案） |
| 1 | 四角同心弧刻度（r=20..92 步进 4，主刻度 24/48/72 加粗 3px） | 肉眼直读：哪圈弧与遮罩边缘重合 |
| 2 | 可调单弧（console `RAD <px>`，1-240） | 精修：弧线恰好贴住遮罩边缘时的半径值 |

## 测量流程

1. `NEXT` 切到 radius 应用（安装顺序 clock → test → radius），默认页 0 全白屏
2. 手机**正对**屏幕拍照（避免透视），照片交给 `tools/measure_radius.py` 分析
3. 点按切页 1 肉眼读粗值；页 2 用 `RAD <px>` 二分精修交叉验证
4. 记录四角半径（可能不等，制造公差）

## 生命周期

| 回调 | 实现 |
|------|------|
| on_create | NULL |
| on_open | `view_radius_create()`：构建三页 screen（白屏 / 黑底刻度 / 黑底单弧） |
| on_running | 消费 console 的 `RAD` 请求（原子 int 交接，见 app_radius.c） |
| on_close | `view_radius_destroy()` |

## 文件构成

- `app_radius.c/h`：生命周期 + console→gui 原子参数交接
- `view/view_radius.c/h`：三页 UI + 自绘弧线 widget（class 继承模式，对照
  `view_dotmatrix.c`；`lv_draw_line` 逐段画弧，无持久缓冲）

## 验证方式

- `RAD 40` 后 SNAP 截图，四角应见半径 40px 的白色弧线
- 触摸点按翻页；左右滑动切应用（swipe_nav 不受影响）

## 注意

- 页 0 全白屏是 AMOLED 峰值功耗场景，测量完立即切页/切应用，勿长时间停留
- 新增源文件目录后需 `idf.py reconfigure`（GLOB 仅在 configure 时求值）
