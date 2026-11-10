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
- 组件只依赖 HAL 与 LVGL，不依赖任何具体 app
- 目前暂无共享组件（仅一个 app_ok）
