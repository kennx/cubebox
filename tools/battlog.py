#!/usr/bin/env python3
"""CubeBox 电池放电斜率工具（配合固件 BATT / BATTLOG 命令）。

固件侧每 60s 把 电池% + 电压mV + 标志 写入 RAM 环形缓冲（重启即失，~17h 容量）：
  flags: bit0=低功耗 bit1=VBUS在位 bit2=充电中

用法（用系统 python，需要 pyserial）：
  python3 tools/battlog.py -p /dev/ttyACM0 --batt            # 即时读一次
  python3 tools/battlog.py -p /dev/ttyACM0 --dump            # 导出并分析斜率
  python3 tools/battlog.py -p /dev/ttyACM0 --dump -o log.csv # 另存 CSV
  python3 tools/battlog.py -p /dev/ttyACM0 --dump --mah 1000 # 指定电池容量（默认 1000）
"""
import argparse
import csv
import re
import sys
import time

import serial

# 拔线放电测试步骤（重要：日志只存在 RAM 里，重启就没了）：
#   1. 插着 USB 时先执行 --batt 确认读数正常
#   2. 拔掉 USB，把设备放着别碰（60s 后自动进入低功耗），放几小时或过夜
#   3. 插回 USB，立刻执行 --dump（不要先重启/烧录！）


def cmd_batt(ser):
    ser.reset_input_buffer()
    ser.write(b"BATT\n")
    ser.flush()
    deadline = time.time() + 5
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        s = line.decode("ascii", "ignore").strip()
        if "BATT pct=" in s:
            print(s[s.index("BATT pct="):])
            return
    sys.exit("ERROR: no BATT response within 5s")


def cmd_dump(ser, args):
    ser.reset_input_buffer()
    ser.write(b"BATTLOG DUMP\n")
    ser.flush()

    # 串口行偶尔与每秒走时日志粘连，改为整体文本流 + 正则提取，免疫行撕裂
    buf = []
    deadline = time.time() + 90
    while time.time() < deadline:
        line = ser.readline()
        if line:
            buf.append(line.decode("ascii", "ignore"))
            if " BL END" in line.decode("ascii", "ignore"):
                break
    text = "".join(buf)

    entries = []
    for m in re.finditer(r"BL (\d+) (\d+) (\d+) (\d+)", text):
        entries.append((int(m.group(1)), int(m.group(2)),
                        int(m.group(3)), int(m.group(4))))
    head = re.search(r"BL HEAD n=(\d+)", text)
    total_n = int(head.group(1)) if head else None

    if not entries:
        sys.exit("ERROR: no BL lines received (固件没回 BATTLOG DUMP？检查端口)")

    # CSV 导出
    if args.out:
        with open(args.out, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["uptime_s", "pct", "vbat_mv", "lowpower", "vbus", "charging"])
            for t, pct, mv, flags in entries:
                w.writerow([t, pct, mv, flags & 1, (flags >> 1) & 1, (flags >> 2) & 1])

    print(f"收到 {len(entries)} 条" + (f"（设备报告 {total_n} 条）" if total_n is not None else ""))
    if len(entries) < 2:
        print("样本太少，至少要 2 条（60s 一条），先放着再测")
        return

    def hms(sec):
        return f"{sec // 3600}h{(sec % 3600) // 60:02d}m"

    print(f"\n记录区间: {hms(entries[0][0])} -> {hms(entries[-1][0])}"
          f"  电量 {entries[0][1]}% -> {entries[-1][1]}%"
          f"  电压 {entries[0][2]}mV -> {entries[-1][2]}mV")

    # 只取放电段（VBUS 不在位）的连续片段
    runs = []
    run = []
    for e in entries:
        if (e[3] & 2) == 0:
            run.append(e)
        else:
            if len(run) >= 2:
                runs.append(run)
            run = []
    if len(run) >= 2:
        runs.append(run)

    if not runs:
        print("\n没有检测到放电段（所有记录都在 USB 供电下）。拔线测试后再导出。")
        return

    print(f"\n放电段数: {len(runs)}")
    for i, r in enumerate(runs):
        dur_h = (r[-1][0] - r[0][0]) / 3600.0
        drop = r[0][1] - r[-1][1]
        lp_cnt = sum(1 for e in r if e[3] & 1)
        mv_drop = r[0][2] - r[-1][2]
        print(f"\n[放电段 {i + 1}] {hms(r[0][0])} -> {hms(r[-1][0])}"
              f"  时长 {dur_h:.2f}h  低功耗占比 {lp_cnt}/{len(r)}")
        print(f"  电量 {r[0][1]}% -> {r[-1][1]}%（-{drop}%）  电压 {r[0][2]}mV -> {r[-1][2]}mV（-{mv_drop}mV）")
        if dur_h > 0.05 and drop > 0:
            slope = drop / dur_h
            cur = slope / 100.0 * args.mah
            print(f"  斜率 {slope:.2f}%/h  ≈ {cur:.1f}mA  "
                  f"满电续航 ≈ {100 / slope:.1f}h（{100 / slope / 24:.1f} 天）")
            last_pct = r[-1][1]
            if last_pct > 0:
                print(f"  剩余续航 ≈ {last_pct / slope:.1f}h")
        else:
            print("  （时长或电量变化太小，算不出斜率；放久一点再测）")

    print(f"\n（按 {args.mah}mAh 标称容量折算，可用容量通常再打 85-90 折；"
          f"电量计 1% 分辨率，短时段误差大，过夜数据最准）")


def main():
    ap = argparse.ArgumentParser(description="CubeBox battery discharge logger")
    ap.add_argument("-p", "--port", required=True, help="USB-Serial/JTAG port, e.g. /dev/ttyACM0")
    ap.add_argument("--batt", action="store_true", help="即时读取一次电池状态")
    ap.add_argument("--dump", action="store_true", help="导出 BATTLOG 并分析放电斜率")
    ap.add_argument("-o", "--out", help="可选：把记录存成 CSV")
    ap.add_argument("--mah", type=int, default=1000, help="电池标称容量 mAh（默认 1000）")
    args = ap.parse_args()

    if not args.batt and not args.dump:
        ap.error("需要 --batt 或 --dump 之一")

    ser = serial.Serial(args.port, 115200, timeout=1)
    try:
        if args.batt:
            cmd_batt(ser)
        if args.dump:
            cmd_dump(ser, args)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
