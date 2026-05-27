#!/usr/bin/env python3
"""
M5Stick-P1 交互式串口监视器

用法:
  python3 tools/serial_mon.py
  python3 tools/serial_mon.py --port /dev/cu.usbserial-XXXX --baud 115200

快捷键:
  Ctrl+]  退出
  Ctrl+L  清屏
  Ctrl+S  保存日志到文件
"""

import serial
import sys
import os
import argparse
import threading
import time
from datetime import datetime

# ─── 配置 ───────────────────────────────────────────────

SERIAL_PORT = "/dev/cu.usbserial-4D52671EFA"
BAUD = 115200

# ─── ANSI 颜色 ──────────────────────────────────────────

class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"


def reader_thread(ser, stop_event):
    """后台线程：持续读取串口数据并打印"""
    while not stop_event.is_set():
        try:
            waiting = ser.in_waiting
            if waiting > 0:
                data = ser.read(waiting)
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
            else:
                time.sleep(0.01)
        except (serial.SerialException, OSError):
            break


def main():
    parser = argparse.ArgumentParser(description="M5Stick-P1 Interactive Serial Monitor")
    parser.add_argument("--port", default=SERIAL_PORT, help=f"Serial port (default: {SERIAL_PORT})")
    parser.add_argument("--baud", type=int, default=BAUD, help=f"Baud rate (default: {BAUD})")
    parser.add_argument("--log", help="Save all output to log file")
    args = parser.parse_args()

    port = args.port
    baud = args.baud

    if not os.path.exists(port):
        print(f"{Colors.YELLOW}[WARN] Port {port} does not exist{Colors.RESET}")
        print(f"Available ports:")
        for p in os.listdir("/dev/"):
            if "cu." in p or "tty." in p:
                print(f"  /dev/{p}")
        sys.exit(1)

    # 打开串口
    print(f"{Colors.CYAN}Connecting to {port} @ {baud}...{Colors.RESET}")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"{Colors.YELLOW}[FATAL] {e}{Colors.RESET}")
        sys.exit(1)

    # 短暂的启动延迟
    time.sleep(1.5)

    # 清空缓冲区
    ser.reset_input_buffer()

    # 启动读取线程
    stop_event = threading.Event()
    reader = threading.Thread(target=reader_thread, args=(ser, stop_event), daemon=True)
    reader.start()

    # 日志文件
    log_file = None
    if args.log:
        log_file = open(args.log, "a", encoding="utf-8")
        log_file.write(f"\n=== Session started at {datetime.now().isoformat()} ===\n")

    print(f"{Colors.GREEN}Connected!{Colors.RESET}")
    print(f"  {Colors.DIM}Ctrl+] to quit, Ctrl+L to clear screen{Colors.RESET}")
    print(f"  {Colors.DIM}Type 'help' for Xeros Shell commands{Colors.RESET}")
    print(f"{'─'*50}")

    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                break

            # Ctrl+] (ASCII 29 = GS) → quit
            if line.strip() == "\x1d":
                print(f"\n{Colors.GREEN}Goodbye!{Colors.RESET}")
                break

            # Ctrl+L (ASCII 12 = FF) → clear
            if line.strip() == "\x0c":
                os.system("clear" if os.name != "nt" else "cls")
                continue

            # 写入串口
            try:
                ser.write(line.encode())
                if log_file:
                    log_file.write(f"[SEND] {line}")
            except serial.SerialException:
                print(f"\n{Colors.YELLOW}[WARN] Serial disconnected{Colors.RESET}")
                break

    except KeyboardInterrupt:
        print(f"\n{Colors.GREEN}Interrupted. Goodbye!{Colors.RESET}")

    finally:
        stop_event.set()
        reader.join(timeout=1)
        ser.close()
        if log_file:
            log_file.write(f"=== Session ended at {datetime.now().isoformat()} ===\n")
            log_file.close()
            print(f"{Colors.DIM}Log saved to {args.log}{Colors.RESET}")


if __name__ == "__main__":
    main()
