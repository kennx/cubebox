#!/usr/bin/env bash
# 一键：编译 + 烧录 + 自动把电脑本机时间同步到 PCF85063 RTC
# 用法：tools/flash.sh [PORT]   （默认 /dev/cu.usbmodem1101）
set -euo pipefail

PORT="${1:-/dev/cu.usbmodem1101}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 未进入 IDF 环境时，自动加载项目指定的 v5.5.1
if ! command -v idf.py >/dev/null 2>&1; then
    source "$HOME/.espressif/v5.5.1/esp-idf/export.sh" >/dev/null
fi

cd "$PROJECT_DIR"
idf.py -p "$PORT" build flash

# 烧录后 esptool 硬复位设备，USB-Serial/JTAG 会重新枚举，串口节点短暂失效
sleep 3
# 同步脚本会再次复位以对齐 time_sync 就绪横幅
python "$SCRIPT_DIR/sync_rtc.py" --port "$PORT"
