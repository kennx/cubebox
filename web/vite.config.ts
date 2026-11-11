import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// 部署于 GitHub Pages 子路径 https://kennx.github.io/cubebox/（Task 5 启用 base）
export default defineConfig({
  plugins: [react()],
});
