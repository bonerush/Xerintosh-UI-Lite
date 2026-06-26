#!/usr/bin/env python3
"""
Xeros 内核调试工具

使用方法:
    python3 tools/xeros_debug.py /dev/ttyUSB0
    python3 tools/xeros_debug.py /dev/ttyUSB0 --reset
    python3 tools/xeros_debug.py /dev/ttyUSB0 --cmd "ps"
    python3 tools/xeros_debug.py /dev/ttyUSB0 --monitor --log debug.log
    python3 tools/xeros_debug.py /dev/ttyUSB0 --smp --duration 3
    python3 tools/xeros_debug.py /dev/ttyUSB0 --tickless --duration 10

功能:
    - 连接串口 (115200 baud)
    - 复位设备 (DTR/RTS)
    - 实时查看日志
    - 发送命令并收集输出
    - 保存日志到文件
    - 等待特定字符串 (用于自动化测试)
    - SMP 核状态采样 (--smp)
    - tickless 平均 tick 间隔统计 (--tickless)
    - 自动检测 Guru Meditation 与复位原因
"""

import serial
import time
import sys
import threading
import argparse
import re
from typing import Optional
from collections import defaultdict


class XerosDebugger:
    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.running = False
        self.log_lines: list[str] = []
        self.tick_records: list[float] = []
        self.smp_samples: list[dict] = []
        self.guru_count = 0
        self.reset_count = 0
        self.last_reset_reason: Optional[str] = None
        self.last_guru_message: Optional[str] = None

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
        """复位设备 (通过 RTS 脉冲拉低 EN)"""
        if not self.ser:
            print("[FAIL] 未连接")
            return False

        print("[...] 正在复位设备...")
        # RTS=True 拉低 EN 使设备复位；保持 200ms 后释放。
        # 不碰 DTR，避免 GPIO0 被拉低进入下载模式导致二次复位。
        self.ser.rts = True
        time.sleep(0.2)
        self.ser.rts = False
        time.sleep(0.1)
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
                    alert = self.detect_anomalies(text)
                    if alert:
                        print(alert)
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
                    alert = self.detect_anomalies(text)
                    if alert:
                        print(alert)
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
                        stripped = text.strip()
                        if stripped:
                            self.log_lines.append(stripped)
                            self.record_tick(stripped)
                            alert = self.detect_anomalies(stripped)
                            if alert:
                                sys.stdout.write(alert + "\n")
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
        result = self.read_until(r'\[\s*BOOT\s*\]|app_main|Xeros', timeout)
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

    def save_log(self, filename: str) -> None:
        """保存收集的日志"""
        with open(filename, 'w') as f:
            for line in self.log_lines:
                f.write(line + '\n')
        print(f"[OK] 日志已保存到 {filename} ({len(self.log_lines)} 行)")

    def detect_anomalies(self, line: str) -> Optional[str]:
        """检测 Guru Meditation 与复位原因，返回告警文本"""
        alert: Optional[str] = None
        if "Guru Meditation Error" in line:
            self.guru_count += 1
            self.last_guru_message = line
            alert = f"[ALERT] Guru Meditation 检测到 ({self.guru_count} 次): {line[:120]}"
        rst_match = re.search(r'rst:0x[0-9A-Fa-f]+\s+\(([^)]+)\)', line)
        if rst_match:
            self.reset_count += 1
            self.last_reset_reason = rst_match.group(1)
            alert = f"[ALERT] 设备复位 ({self.reset_count} 次): {rst_match.group(1)}"
        return alert

    def parse_ps_output(self, output: list[str]) -> dict:
        """解析 Shell `ps` 命令输出，返回 {cpu: [task_name, ...]}"""
        tasks_by_cpu: dict[int, list[str]] = defaultdict(list)
        # 典型行: "  0 RUNNING       xidle0   cpu=0"
        ps_re = re.compile(
            r'^\s*(\d+)\s+(RUNNING|READY|SLEEPING|ZOMBIE)\s+(\S+)\s+cpu=(\d+|ANY)'
        )
        for line in output:
            m = ps_re.match(line)
            if not m:
                continue
            cpu_field = m.group(4)
            if cpu_field == "ANY":
                cpu = -1
            else:
                cpu = int(cpu_field)
            state = m.group(2)
            name = m.group(3)
            if state == "RUNNING":
                tasks_by_cpu[cpu].append(name)
        return dict(tasks_by_cpu)

    def sample_smp(self, duration: float = 2.0, interval: float = 0.5) -> dict:
        """多次采样 ps 输出，统计每个核的 RUNNING 任务"""
        samples: list[dict] = []
        end = time.time() + duration
        while time.time() < end:
            output = self.collect_command("ps", timeout=1.0)
            parsed = self.parse_ps_output(output)
            samples.append(parsed)
            time.sleep(interval)

        # 聚合：每个核出现过的任务及最近一个 RUNNING 任务
        summary: dict[int, dict] = {}
        for parsed in samples:
            for cpu, names in parsed.items():
                if cpu not in summary:
                    summary[cpu] = {"running_now": names[-1] if names else None,
                                    "seen": set()}
                for name in names:
                    summary[cpu]["seen"].add(name)
        for info in summary.values():
            info["seen"] = sorted(info["seen"])
        self.smp_samples = samples
        return summary

    def record_tick(self, line: str) -> None:
        """从日志行中记录 tick 时间戳 (依赖 NATIVE_SCHED_DEBUG 输出)"""
        # 匹配类似 "[native] switch_to: ..." 或自定义 tick 日志
        if "[native]" in line:
            self.tick_records.append(time.time())

    def tickless_stats(self, duration: float = 5.0) -> dict:
        """统计一段时间内 tick 日志的平均间隔"""
        self.tick_records.clear()
        print(f"[...] 开始 tickless 采样 {duration}s，请在设备侧保持运行")
        end = time.time() + duration
        while time.time() < end:
            lines = self.read_output(timeout=0.2)
            for line in lines:
                self.record_tick(line)
                alert = self.detect_anomalies(line)
                if alert:
                    print(alert)
            time.sleep(0.05)

        if len(self.tick_records) < 2:
            return {
                "count": len(self.tick_records),
                "avg_ms": None,
                "min_ms": None,
                "max_ms": None,
                "note": "未检测到 [native] 调试日志；如需统计，请打开 NATIVE_SCHED_DEBUG"
            }

        intervals: list[float] = []
        for i in range(1, len(self.tick_records)):
            intervals.append((self.tick_records[i] - self.tick_records[i - 1]) * 1000.0)
        return {
            "count": len(self.tick_records),
            "avg_ms": sum(intervals) / len(intervals),
            "min_ms": min(intervals),
            "max_ms": max(intervals),
            "note": "基于 [native] 调试日志时间戳"
        }


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
    parser.add_argument('--smp', action='store_true', help='采样 SMP 核状态')
    parser.add_argument('--tickless', action='store_true', help='统计 tickless 平均 tick 间隔')
    parser.add_argument('--duration', type=float, default=5.0, help='SMP/tickless 采样时长(秒)')
    parser.add_argument('--interval', type=float, default=0.5, help='SMP 采样间隔(秒)')

    args = parser.parse_args()

    dbg = XerosDebugger(args.port, args.baud)
    if not dbg.connect():
        sys.exit(1)

    try:
        if args.reset:
            dbg.reset()

        if args.wait_boot:
            dbg.wait_for_boot(args.timeout)

        if args.cmd:
            output = dbg.collect_command(args.cmd, timeout=args.timeout)
            for line in output:
                print(line)

        if args.smp:
            print(f"[...] SMP 采样 {args.duration}s，间隔 {args.interval}s")
            summary = dbg.sample_smp(duration=args.duration, interval=args.interval)
            print("[OK] SMP 状态摘要:")
            for cpu in sorted(summary.keys()):
                info = summary[cpu]
                print(f"  CPU {cpu}: 当前 RUNNING={info['running_now']}, 采样期间 seen={info['seen']}")
            if not summary:
                print("  (未解析到 RUNNING 任务，请确认已连接并发送 ps 命令)")

        if args.tickless:
            stats = dbg.tickless_stats(duration=args.duration)
            print("[OK] Tickless 统计:")
            for key, value in stats.items():
                if value is None:
                    print(f"  {key}: N/A")
                elif isinstance(value, float):
                    print(f"  {key}: {value:.3f} ms")
                else:
                    print(f"  {key}: {value}")

        if args.monitor:
            dbg.monitor(log_file=args.log)

        if args.log and not args.monitor:
            dbg.save_log(args.log)

    finally:
        dbg.disconnect()


if __name__ == '__main__':
    main()
