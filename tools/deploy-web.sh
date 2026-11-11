#!/usr/bin/env bash
# 构建网页配置器并发布到 gh-pages 孤儿分支（构建产物 = 分支全部内容）。
# 用法：tools/deploy-web.sh [--push]
#   --push  提交后推送 origin gh-pages
# 注意：首次部署后需在 GitHub 仓库 Settings → Pages 选择 gh-pages 分支（/root）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PUSH=0
[ "${1:-}" = "--push" ] && PUSH=1

echo "deploy: 构建 web/"
cd "$ROOT/web"
npm ci --silent
npm run build --silent
[ -f dist/index.html ] || { echo "deploy: dist/index.html 缺失" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'git -C "$ROOT" worktree remove --force "$TMP" 2>/dev/null || rm -rf "$TMP"' EXIT

git -C "$ROOT" worktree add --detach "$TMP" HEAD
if git -C "$ROOT" show-ref --verify --quiet refs/heads/gh-pages; then
    git -C "$TMP" checkout --quiet gh-pages
else
    git -C "$TMP" checkout --quiet --orphan gh-pages
fi
# 清空既有内容（首次 orphan 清 HEAD 残留、复跑清旧部署），再拷入新构建
git -C "$TMP" rm -rf -q . 2>/dev/null || true
cp -r "$ROOT/web/dist/." "$TMP/"
git -C "$TMP" add -A

if git -C "$TMP" diff --cached --quiet; then
    echo "deploy: 构建产物与 gh-pages 一致，无变更提交"
else
    git -C "$TMP" commit -q -m "deploy: 网页配置器 $(date +%F)"
    echo "deploy: 已提交 $(git -C "$TMP" log --oneline -1)"
fi

if [ "$PUSH" = 1 ]; then
    git -C "$ROOT" push origin gh-pages
    echo "deploy: 已推送 origin gh-pages"
else
    echo "deploy: 未推送（--push 推送；仓库 Settings → Pages 选 gh-pages 分支 /root）"
fi
