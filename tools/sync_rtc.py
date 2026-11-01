#!/usr/bin/env python3
"""把电脑本机时间写入 CubeBox 的 PCF85063 RTC。

流程：DTR/RTS 复位设备 → 等待固件 time_sync ready 横幅 →
发送 SETTIME YYYY-MM-DD HH:MM:SS → 校验 RTC SET OK 读回偏差 ≤1s。

依赖 pyserial（ESP-IDF Python 环境自带）。

用法：python tools/sync_rtc.py [--port /dev/cu.usbmodem1101] [--baud 115200]
"""
import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial：请在 ESP-IDF 环境下运行（source export.sh），"
             "或 pip install pyserial")

READY_BANNER = "time_sync ready"
SET_OK = "RTC SET OK"


def open_serial(port, baud, retries=10):
    """打开串口；设备复位导致 USB 重新枚举时节点短暂消失，带重试。"""
    for i in range(retries):
        try:
            return serial.Serial(port, baud, timeout=1)
        except serial.SerialException:
            if i == retries - 1:
                raise
            time.sleep(1)


def sync(port, baud):
    try:
        ser = open_serial(port, baud)
    except serial.SerialException as e:
        print(f"FAIL: 无法打开串口 {port}（设备是否已连接？）：{e}")
        return 1

    # 打开串口后通过 DTR/RTS 复位芯片，确保从启动日志开始就能观察到
    # time_sync 任务的就绪横幅（若任务已在运行，横幅早已打印，无法对齐时序）
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False

    def readline():
        """读一行；复位引发的 USB 重新枚举会使节点失效，重开后继续。"""
        nonlocal ser
        try:
            return ser.readline().decode(errors="replace").strip()
        except serial.SerialException:
            try:
                ser.close()
            except serial.SerialException:
                pass
            time.sleep(1)
            ser = open_serial(port, baud)
            return ""

    saw_ready = False
    deadline = time.time() + 20
    while time.time() < deadline:
        line = readline()
        if not line:
            continue
        print("RX:", line)
        if READY_BANNER in line:
            saw_ready = True
            break
        if "time sync disabled" in line or "driver install failed" in line:
            print("FAIL: 设备端 USB-Serial/JTAG 驱动安装失败，无法对时")
            ser.close()
            return 1
    if not saw_ready:
        print(f"FAIL: {READY_BANNER} 秒内未出现就绪横幅，固件是否包含 time_sync 任务？")
        ser.close()
        return 1

    # 尽量晚地取本机时间，减小传输偏差
    cmd_time = time.localtime()
    cmd = "SETTIME {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}\n".format(*cmd_time[:6])
    print(f"[{time.strftime('%H:%M:%S', time.localtime())}] TX: {cmd.strip()}")
    ser.write(cmd.encode())

    set_ok_line = None
    display_times = []
    deadline = time.time() + 8
    while time.time() < deadline:
        line = readline()
        if not line:
            continue
        print("RX:", line)
        if SET_OK in line:
            set_ok_line = line
        m = re.search(r"Updated display: (.+?) \| (\d\d:\d\d:\d\d)", line)
        if m:
            display_times.append((m.group(1), m.group(2)))
        if set_ok_line and len(display_times) >= 2:
            break
    ser.close()

    if set_ok_line is None:
        print("FAIL: 未收到 RTC SET OK 确认")
        return 1

    def secs(t):
        return int(t[0:2]) * 3600 + int(t[3:5]) * 60 + int(t[6:8])

    rtc_t = re.search(r"(\d\d:\d\d:\d\d)$", set_ok_line).group(1)
    sent_t = cmd.split()[2]
    diff = abs(secs(rtc_t) - secs(sent_t))
    print(f"\nRTC 读回: {rtc_t}  发送: {sent_t}  偏差: {diff}s")
    if display_times:
        print("屏幕显示:", display_times[-1])
    if diff > 1:
        print("FAIL: 偏差超过 1s")
        return 1
    print("PASS: RTC 已同步为电脑本机时间")
    return 0


def main():
    ap = argparse.ArgumentParser(description="把电脑本机时间写入 CubeBox 的 PCF85063 RTC")
    ap.add_argument("--port", default="/dev/cu.usbmodem1101", help="串口设备路径")
    ap.add_argument("--baud", type=int, default=115200, help="波特率")
    args = ap.parse_args()
    sys.exit(sync(args.port, args.baud))


if __name__ == "__main__":
    main()
