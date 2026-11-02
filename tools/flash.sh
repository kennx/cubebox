#!/usr/bin/env bash
# 一键：编译 + 烧录 + 自动把电脑本机时间同步到 PCF85063 RTC
# 用法：tools/flash.sh [PORT]   （默认自动探测 /dev/ttyACM* 或 /dev/cu.usbmodem*）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
    PORT="$(ls /dev/ttyACM* /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    [[ -n "$PORT" ]] || { echo "未指定 PORT 且未发现串口设备" >&2; exit 1; }
fi

# 未进入 IDF 环境时，自动加载本机 IDF（优先 ~/esp/esp-idf，其次旧机器布局）
if ! command -v idf.py >/dev/null 2>&1; then
    if [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
        export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.12_env}"
        source "$HOME/esp/esp-idf/export.sh" >/dev/null
    else
        source "$HOME/.espressif/v5.5.1/esp-idf/export.sh" >/dev/null
    fi
fi

cd "$PROJECT_DIR"
idf.py -p "$PORT" build flash

# 烧录后 esptool 硬复位设备，USB-Serial/JTAG 会重新枚举，串口节点短暂失效
sleep 3
# 同步脚本会再次复位以对齐 time_sync 就绪横幅
python "$SCRIPT_DIR/sync_rtc.py" --port "$PORT"
