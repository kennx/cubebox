# CubeBox 网页配置器（web/）

React 单页应用，经 **Web Serial API**（USB）直连设备进行配置：Wi-Fi 凭据、屏幕亮度、时区。
部署于 GitHub Pages：`https://kennx.github.io/cubebox/`。

## 开发

```bash
cd web
npm install
npm run dev        # http://localhost:5173/cubebox/（base 已设 /cubebox/，dev 也在子路径）
```

- **无设备开发/演示**：`http://localhost:5173/cubebox/?mock=1` 使用 localStorage 模拟设备（连接、读改配置、持久化全部可走通）。
- **真机**：Chrome/Edge 打开（Web Serial 仅 Chromium 系支持），插 USB 后点「授权并连接」（串口被 monitor/screenshot.py 占用时会打开失败，先关闭占用程序）。
- 协议规格见 `docs/features/web-config.md`。

## 构建与部署

```bash
npm run build                 # 产物 web/dist（vite base 已设 /cubebox/）
tools/deploy-web.sh           # 构建并提交到 gh-pages 孤儿分支
tools/deploy-web.sh --push    # 同时推送 origin gh-pages
```

首次部署后需在 GitHub 仓库 **Settings → Pages** 选择 `gh-pages` 分支（目录 `/root`），此后每次 push 即上线。
