# apps/common/

跨应用共享的通用实现（对照 StackChan firmware/main/apps/common/：状态栏、
toast、加载页等）。

## 什么放这里

- 多个 app 复用的 UI 组件（如状态栏、提示框、通用列表样式）
- 多个 app 复用的非硬件工具函数

只被一个 app 使用的东西留在该 app 自己的目录里；等出现第二个使用者再搬来
（避免过早抽象）。

## 约定

- 每个共享组件一个子目录：`common/<component>/<component>.{c,h}`
- 组件不依赖任何具体 app（可用 HAL、LVGL 与 apps 框架）
- 现有组件：
  - `dotmatrix/`：23×23 点阵（`dotmatrix.{c,h}` 纯逻辑网格+3×5 字体表，主机可测；`view_dotmatrix.{c,h}` LVGL 差量绘制 widget）
  - `swipe_nav/`：左右滑动手势导航（挂 app screen，循环切换走 `apps_request_open`）
