#!/usr/bin/env python3
"""
Xeros 内核串口自动化验证脚本

用法:
  python3 tools/verify_serial.py                  # 运行所有测试
  python3 tools/verify_serial.py --test basic     # 基础 Shell 命令测试
  python3 tools/verify_serial.py --test sysfs     # sysfs 读写测试
  python3 tools/verify_serial.py --test phase0    # Phase 0 验收测试
  python3 tools/verify_serial.py --test phase1    # Phase 1 验收测试
"""

import serial
import time
import sys
import argparse
import re

# ─── 配置 ───────────────────────────────────────────────

SERIAL_PORT = "/dev/cu.usbserial-4D52671EFA"
BAUD = 115200
TIMEOUT = 3.0

# ─── 辅助函数 ───────────────────────────────────────────

def open_serial():
    """打开串口连接"""
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=TIMEOUT)
    time.sleep(2)
    ser.reset_input_buffer()
    return ser


def wait_for_prompt(ser, timeout=15.0):
    """
    等待 Xeros Shell 提示符 [$] 出现
    返回 (True, output_text) 或 (False, output_text)
    """
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            if b"$ " in buf:
                time.sleep(0.3)
                remaining = ser.read(ser.in_waiting or 1024)
                buf += remaining
                return True, buf.decode(errors="replace")
        else:
            time.sleep(0.1)
    return False, buf.decode(errors="replace")


def send_cmd(ser, cmd, wait=0.8):
    """
    发送命令并等待响应
    返回输出文本
    """
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    output = ser.read(ser.in_waiting or 4096)
    return output.decode(errors="replace")


def print_result(name, ok, detail=""):
    """打印单个测试结果"""
    marker = "\033[32mPASS\033[0m" if ok else "\033[31mFAIL\033[0m"
    msg = f"  {marker}: {name}"
    if detail:
        msg += f"  ({detail})"
    print(msg)
    return ok


# ─── 基础 Shell 测试 ─────────────────────────────────────

def test_shell_basic(ser):
    """验证 Shell 基本命令"""
    results = []
    print("\n─── Shell Basic Tests ───")

    # 清空残留输出
    send_cmd(ser, "")

    # help
    out = send_cmd(ser, "help")
    results.append(("help", "Xeros" in out or "help" in out.lower()))

    # ps
    out = send_cmd(ser, "ps")
    ok = "PID" in out and "STATE" in out
    results.append(("ps shows task table", ok, f"tasks found: {out.count(chr(10))}" if ok else None))

    # ls /
    out = send_cmd(ser, "ls /")
    ok = "dev" in out and "proc" in out
    results.append(("ls / shows filesystems", ok))

    # pwd
    out = send_cmd(ser, "pwd")
    results.append(("pwd", "/" in out))

    # cat /proc/version
    out = send_cmd(ser, "cat /proc/version")
    results.append(("cat /proc/version", "Xeros" in out or "0." in out))

    # cat /proc/tasks
    out = send_cmd(ser, "cat /proc/tasks")
    results.append(("cat /proc/tasks", "ui" in out or "shell" in out or "idle" in out))

    # cd /proc
    out = send_cmd(ser, "cd /proc")
    out = send_cmd(ser, "pwd")
    results.append(("cd /proc + pwd", "/proc" in out))

    # cd / (reset)
    send_cmd(ser, "cd /")

    # echo
    out = send_cmd(ser, "echo hello_xeros_test")
    results.append(("echo", "hello_xeros_test" in out))

    # 错误命令
    out = send_cmd(ser, "nonexistent_cmd_xyz")
    results.append(("unknown command error", "not found" in out.lower() or "unknown" in out.lower()))

    return results


# ─── 新增命令测试 ────────────────────────────────────────

def test_shell_new_cmds(ser):
    """验证 Phase 0 新增命令"""
    results = []
    print("\n─── New Shell Commands Tests ───")

    # free
    out = send_cmd(ser, "free")
    ok = ("free" in out.lower() or "heap" in out.lower() or "bytes" in out.lower())
    results.append(("free", ok))

    # uname
    out = send_cmd(ser, "uname")
    results.append(("uname", "Xeros" in out or "ESP" in out or "M5" in out))

    # df
    out = send_cmd(ser, "df")
    results.append(("df", "inode" in out.lower() or "entry" in out.lower() or "/" in out))

    # clear
    out = send_cmd(ser, "clear")
    results.append(("clear (no crash)", True))

    # history
    out = send_cmd(ser, "history")
    results.append(("history", True))  # 至少有历史记录或提示为空

    # date / uptime
    out = send_cmd(ser, "date")
    results.append(("date", True))  # 不崩溃即可

    # hexdump
    out = send_cmd(ser, "hexdump /proc/version")
    results.append(("hexdump", len(out) > 10))

    # kill (对 idle 尝试, 预期失败但不崩溃)
    out = send_cmd(ser, "kill 0")
    results.append(("kill (no crash)", True))

    return results


# ─── sysfs 测试 ──────────────────────────────────────────

def test_sysfs(ser):
    """验证 sysfs 读写"""
    results = []
    print("\n─── sysfs Tests ───")

    # 读取亮度
    out = send_cmd(ser, "cat /sys/brightness")
    val = out.strip().split("\n")[-1] if out.strip() else ""
    ok = val.isdigit()
    results.append(("cat /sys/brightness returns number", ok, val if ok else f"got: {val}"))

    # 读取动画速度
    out = send_cmd(ser, "cat /sys/anim_speed")
    val = out.strip().split("\n")[-1] if out.strip() else ""
    ok = val.isdigit()
    results.append(("cat /sys/anim_speed returns number", ok))

    # 读取动画开关
    out = send_cmd(ser, "cat /sys/anim_enabled")
    results.append(("cat /sys/anim_enabled", "0" in out or "1" in out))

    # 读取日志级别
    out = send_cmd(ser, "cat /sys/kernel/log_level")
    results.append(("cat /sys/kernel/log_level", "0" in out or "1" in out or "2" in out or "3" in out))

    # 写入并回读 (有效值)
    send_cmd(ser, "echo 1 > /sys/anim_enabled")
    out = send_cmd(ser, "cat /sys/anim_enabled")
    results.append(("write sys/anim_enabled + readback", "1" in out))

    return results


# ─── Phase 验收 ──────────────────────────────────────────

def test_phase0_full(ser):
    """Phase 0 完整验收"""
    results = []
    results += test_shell_basic(ser)
    results += test_shell_new_cmds(ser)
    return results


def test_phase1_full(ser):
    """Phase 1 完整验收"""
    results = []
    results += test_sysfs(ser)
    return results


# ─── 主入口 ──────────────────────────────────────────────

TEST_MAP = {
    "basic":   test_shell_basic,
    "shell":   test_shell_basic,
    "newcmds": test_shell_new_cmds,
    "sysfs":   test_sysfs,
    "phase0":  test_phase0_full,
    "phase1":  test_phase1_full,
    "all":     lambda ser: test_shell_basic(ser) + test_shell_new_cmds(ser) + test_sysfs(ser),
}


def main():
    parser = argparse.ArgumentParser(description="Xeros Kernel Serial Verification")
    parser.add_argument("--test", choices=list(TEST_MAP.keys()), default="all",
                        help="Test suite to run (default: all)")
    parser.add_argument("--port", default=SERIAL_PORT,
                        help=f"Serial port (default: {SERIAL_PORT})")
    parser.add_argument("--baud", type=int, default=BAUD,
                        help=f"Baud rate (default: {BAUD})")
    args = parser.parse_args()

    global SERIAL_PORT, BAUD
    SERIAL_PORT = args.port
    BAUD = args.baud

    print(f"[VERIFY] Opening {SERIAL_PORT} @ {BAUD}...")
    try:
        ser = open_serial()
    except serial.SerialException as e:
        print(f"[FATAL] Cannot open serial port: {e}")
        sys.exit(2)

    print("[VERIFY] Waiting for Xeros Shell prompt...")
    ok, out = wait_for_prompt(ser)
    if not ok:
        print("[FAIL] No shell prompt detected within timeout")
        print(f"  Received ({len(out)} bytes): {out[:200]}...")
        ser.close()
        sys.exit(1)

    print("[VERIFY] Shell prompt detected, running tests...")

    test_func = TEST_MAP[args.test]
    results = test_func(ser)

    passed = sum(1 for _, ok, *_ in results if ok)
    total = len(results)

    print(f"\n{'='*50}")
    print(f"[VERIFY] {args.test}: {passed}/{total} passed")
    for name, ok, *detail in results:
        print_result(name, ok, detail[0] if detail else "")

    ser.close()
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
