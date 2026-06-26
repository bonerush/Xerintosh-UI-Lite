#!/usr/bin/env python3
"""
Xeros 内核调试工具

使用方法:
    python3 tools/xeros_debug.py /dev/ttyUSB0
    python3 tools/xeros_debug.py /dev/ttyUSB0 --reset
    python3 tools/xeros_debug.py /dev/ttyUSB0 --cmd "ps"
    python3 tools/xeros_debug.py /dev/ttyUSB0 --monitor --log debug.log

功能:
    - 连接串口 (115200 baud)
    - 复位设备 (DTR/RTS)
    - 实时查看日志
    - 发送命令并收集输出
    - 保存日志到文件
    - 等待特定字符串 (用于自动化测试)
"""

import serial
import time
import sys
import threading
import argparse
import re


class XerosDebugger:
    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.running = False
        self.log_lines = []

    def connect(self):
        """连接串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                timeout=0.1,
                rtscts=False,
                dsrdtr=False
            )
            # 释放 DTR/RTS，避免 ESP32 在打开串口时被 RTS 拉低 GPIO0 而停在 bootloader
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.05)
            print(f"[OK] 已连接 {self.port} ({self.baud} baud)")
            return True
        except serial.SerialException as e:
            print(f"[FAIL] 无法连接 {self.port}: {e}")
            return False

    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print(f"[OK] 已断开 {self.port}")

    def reset(self):
        """复位设备 (通过 DTR 信号)"""
        if not self.ser:
            print("[FAIL] 未连接")
            return False

        print("[...] 正在复位设备...")
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.dtr = True
        self.ser.rts = False
        time.sleep(0.5)
        self.ser.dtr = False
        print("[OK] 设备已复位")
        return True

    def send_command(self, cmd):
        """发送命令到设备"""
        if not self.ser:
            print("[FAIL] 未连接")
            return False

        self.ser.write(f"{cmd}\n".encode())
        self.ser.flush()
        return True

    def read_output(self, timeout=1.0):
        """读取输出直到超时"""
        if not self.ser:
            return []

        output = []
        start = time.time()
        while time.time() - start < timeout:
            line = self.ser.readline()
            if line:
                text = line.decode('utf-8', errors='replace').strip()
                if text:
                    output.append(text)
                    self.log_lines.append(text)
        return output

    def read_until(self, pattern, timeout=10.0):
        """读取直到匹配指定模式"""
        if not self.ser:
            return None

        start = time.time()
        buffer = []
        while time.time() - start < timeout:
            line = self.ser.readline()
            if line:
                text = line.decode('utf-8', errors='replace').strip()
                if text:
                    buffer.append(text)
                    self.log_lines.append(text)
                    if re.search(pattern, text):
                        return buffer
        return None

    def collect_command(self, cmd, timeout=2.0):
        """发送命令并收集输出"""
        self.send_command(cmd)
        time.sleep(0.1)
        return self.read_output(timeout)

    def monitor(self, log_file=None):
        """实时监控输出"""
        self.running = True
        f = None
        if log_file:
            f = open(log_file, 'w')
            print(f"[OK] 日志保存到 {log_file}")

        print("[...] 开始监控 (Ctrl+C 退出, 输入命令回车发送)")
        print("-" * 60)

        def read_thread():
            while self.running:
                try:
                    line = self.ser.readline()
                    if line:
                        text = line.decode('utf-8', errors='replace')
                        sys.stdout.write(text)
                        sys.stdout.flush()
                        if f:
                            f.write(text)
                            f.flush()
                except serial.SerialException:
                    self.running = False
                    break

        t = threading.Thread(target=read_thread, daemon=True)
        t.start()

        try:
            while self.running:
                cmd = input()
                if cmd.strip() == 'quit':
                    break
                if cmd.strip():
                    self.send_command(cmd)
        except KeyboardInterrupt:
            print("\n[OK] 监控已停止")
        except EOFError:
            pass

        self.running = False
        if f:
            f.close()

    def wait_for_boot(self, timeout=15.0):
        """等待设备启动完成"""
        print("[...] 等待设备启动...")
        result = self.read_until(r'\[BOOT\]|app_main|Xeros', timeout)
        if result:
            print(f"[OK] 设备已启动 (检测到: {result[-1][:50]})")
            return True
        else:
            print("[FAIL] 设备启动超时")
            return False

    def run_test_sequence(self, commands, delay=0.5):
        """运行测试命令序列"""
        results = {}
        for cmd in commands:
            print(f"[...] 执行: {cmd}")
            output = self.collect_command(cmd, timeout=2.0)
            results[cmd] = output
            for line in output:
                print(f"  {line}")
            time.sleep(delay)
        return results

    def save_log(self, filename):
        """保存收集的日志"""
        with open(filename, 'w') as f:
            for line in self.log_lines:
                f.write(line + '\n')
        print(f"[OK] 日志已保存到 {filename} ({len(self.log_lines)} 行)")


def main():
    parser = argparse.ArgumentParser(description='Xeros 内核调试工具')
    parser.add_argument('port', help='串口设备路径 (如 /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--reset', action='store_true', help='复位设备')
    parser.add_argument('--cmd', type=str, help='发送命令')
    parser.add_argument('--monitor', action='store_true', help='实时监控模式')
    parser.add_argument('--log', type=str, help='日志文件路径')
    parser.add_argument('--wait-boot', action='store_true', help='等待设备启动')
    parser.add_argument('--timeout', type=float, default=10.0, help='超时时间(秒)')

    args = parser.parse_args()

    dbg = XerosDebugger(args.port, args.baud)
    if not dbg.connect():
        sys.exit(1)

    try:
        if args.reset:
            dbg.reset()
            time.sleep(1)

        if args.wait_boot:
            dbg.wait_for_boot(args.timeout)

        if args.cmd:
            output = dbg.collect_command(args.cmd, timeout=args.timeout)
            for line in output:
                print(line)

        if args.monitor:
            dbg.monitor(log_file=args.log)

        if args.log and not args.monitor:
            dbg.save_log(args.log)

    finally:
        dbg.disconnect()


if __name__ == '__main__':
    main()
