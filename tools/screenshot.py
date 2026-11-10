#!/usr/bin/env python3
"""从 CubeBox 设备抓取屏幕截图。

固件侧在 USB-Serial/JTAG console 上提供 SNAP 命令：触发一次全屏
invalidate，flush_cb 把最终送显像素按条带以 RGB565 hex
流出（"#SNAP x1 y1 x2 y2" + hex 行），结尾 "#SNAP END"。

用法：python tools/screenshot.py -p /dev/ttyACM0 -o shot.png [--timeout 60]
依赖：pyserial、Pillow（注意：IDF venv 的 python 可能没有 Pillow，用系统 python 跑）
"""
import argparse
import sys
import time

import serial

WIDTH = 480
HEIGHT = 480


def main():
    ap = argparse.ArgumentParser(description="Capture CubeBox screen to PNG")
    ap.add_argument("-p", "--port", required=True, help="USB-Serial/JTAG port, e.g. /dev/ttyACM0")
    ap.add_argument("-o", "--out", required=True, help="output PNG path")
    ap.add_argument("--timeout", type=float, default=60, help="total seconds to wait for the stream")
    args = ap.parse_args()

    ser = serial.Serial(args.port, 115200, timeout=1)
    try:
        ser.reset_input_buffer()
        ser.write(b"SNAP\n")
        ser.flush()

        px = bytearray(WIDTH * HEIGHT * 2)
        got_end = False
        strips = 0
        t0 = time.time()
        while time.time() - t0 < args.timeout:
            line = ser.readline()
            if not line:
                continue
            s = line.decode("ascii", "ignore").strip()
            if s.startswith("#SNAP END"):
                got_end = True
                break
            if not s.startswith("#SNAP "):
                continue  # 忽略混入的日志行
            parts = s.split()
            if len(parts) != 5:
                continue
            try:
                x1, y1, x2, y2 = (int(v) for v in parts[1:])
            except ValueError:
                continue

            # hex 数据行（可能与日志行交错，跳过非十六进制行）
            need = (x2 - x1 + 1) * 4
            hexline = ""
            while len(hexline) < need:
                more = ser.readline().decode("ascii", "ignore").strip()
                if more and all(c in "0123456789abcdefABCDEF" for c in more):
                    hexline += more
            if len(hexline) < need:
                continue
            try:
                data = bytes.fromhex(hexline[:need])
            except ValueError:
                continue

            w = x2 - x1 + 1
            for row in range(y2 - y1 + 1):
                src = row * w * 2
                dst = ((y1 + row) * WIDTH + x1) * 2
                px[dst:dst + w * 2] = data[src:src + w * 2]
            strips += 1
    finally:
        ser.close()

    if not got_end:
        sys.exit(f"ERROR: no '#SNAP END' within {args.timeout}s (strips={strips})")

    from PIL import Image

    img = Image.new("RGB", (WIDTH, HEIGHT))
    pix = img.load()
    for y in range(HEIGHT):
        base = y * WIDTH * 2
        for x in range(WIDTH):
            v = px[base + x * 2] | (px[base + x * 2 + 1] << 8)
            pix[x, y] = (((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
    img.save(args.out)
    print(f"OK {strips} strips -> {args.out}")


if __name__ == "__main__":
    main()
