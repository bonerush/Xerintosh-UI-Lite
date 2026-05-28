# Plan: Xeros 内核优化 & Shell 功能完善 & 去 FreeRTOS 化

**Source**: 用户需求（内核代码分析后的综合优化方案）
**Complexity**: Large
**Target Board**: M5Stick-C (ESP32-PICO) @ `/dev/cu.usbserial-4D52671EFA`
**Verification Method**: 硬件烧录 + Python 串口监控自动化验证

---

## Summary

在现有 Xeros 微内核基础上，分 4 个阶段渐进式优化：

1. **Phase 0 — Shell 命令解析器重构 + 新增命令**（投入产出比最高，立即可验证）
2. **Phase 1 — sysfs ↔ 硬件双向绑定**（让 Shell 真正能控制系统）
3. **Phase 2 — ESP32 调度器去 FreeRTOS 化**（架构纯净度，最大改动）
4. **Phase 3 — procfs 完善 + WiFi/BT 独立任务化**（完善内核功能）

同时附带修复已定位的 3 个串口监视器 Bug（`serial-monitor-3-bugs.plan.md`）。

---

## Design Philosophy

### 渐进式原则

- 每个 Phase 可独立验证（编译 + 烧录 + 串口验证）
- 改动优先触及 Shell 和 sysfs（与 UI 框架解耦，风险最低）
- 调度器改动（Phase 2）放在后期，充分验证前置改动后再进行

### 自动化验证流程

每个 Phase 完成后执行：

```bash
# 1. 编译
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e m5stick-c

# 2. 烧录
pio run -e m5stick-c --target upload

# 3. Python 串口验证脚本
python3 tools/verify_serial.py --test <phase_name>
```

---

## Architecture

```
当前架构                          目标架构 (Phase 2 完成后)
══════════════                    ═════════════════════════
Arduino loop()                    Arduino loop()
  ├─ FreeRTOS task "ui"            ├─ kern_sched_tick()
  │   └─ xTaskCreate               │   ├─ Xeros 协程 "ui"
  │       └─ 信号量令牌             │   │   └─ setjmp/longjmp
  ├─ FreeRTOS task "shell"  →     │   ├─ Xeros 协程 "shell"
  │   └─ xTaskCreate               │   │   └─ setjmp/longjmp
  └─ FreeRTOS task "idle"          │   └─ Xeros 协程 "idle"
      └─ xTaskCreate               │       └─ 复用主栈
                                   │
FreeRTOS 底层 (保持不动):          FreeRTOS 底层 (保持不动):
  - WiFi 协议栈                     - WiFi 协议栈
  - NimBLE 协议栈                   - NimBLE 协议栈
  - M5Unified 定时器                - M5Unified 定时器
```

---

## Patterns to Mirror

| Category | Source | Pattern |
|---|---|---|
| Naming | `hal_display.h:42` | 模块前缀 + snake_case |
| Error handling | `sm_buffer.c` | 返回 int 错误码，上层检查 |
| C/C++ Interop | `hal_display.h:16` | `extern "C"` guards |
| Memory | `ui_item_base.c` | `malloc`/`free`，显式初始化 |
| File size | CLAUDE.md | < 400 lines/file, < 50 lines/function |
| Headers | `kern_shell.h` | include guard + `extern "C"` |

---

## Files to Change

### Phase 0: Shell 重构 + 新增命令

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_shell.c` | REWRITE | 命令解析器重构（tokenize + 精确匹配），新增 8 个命令 |
| `src/kernel/kern_shell.h` | UPDATE | 新增 `kern_shell_register_cmd()` 可扩展命令注册 API |
| `src/kernel/kern_shell_parser.c` | CREATE | 命令行解析器（tokenize + 引号 + 转义） |
| `src/kernel/kern_shell_parser.h` | CREATE | 解析器头文件 |
| `src/kernel/kern_shell_cmds.c` | CREATE | 新增命令实现（free/kill/uname/df/clear/history/date/hexdump） |
| `src/kernel/kern_shell_cmds.h` | CREATE | 命令注册表 |

### Phase 1: sysfs 硬件绑定

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_sysfs.c` | UPDATE | value_ptr 指向 settings 实际全局变量，write 触发回调 |
| `src/kernel/kern_sysfs.h` | UPDATE | 新增 `kern_sysfs_bind()` 动态绑定 API |
| `src/main.cpp` | UPDATE | 初始化后调用 `kern_sysfs_bind()` 挂接硬件变量 |

### Phase 2: 去 FreeRTOS 化

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_task.h` | UPDATE | `kern_ctx_t` 在 ESP32 改为 `jmp_buf` |
| `src/kernel/kern_task.c` | REWRITE (ESP32 分支) | 替换 FreeRTOS 信号量调度为 setjmp/longjmp |
| `src/kernel/kern_ctx_esp32.S` | CREATE | Xtensa 汇编栈切换蹦床 |
| `src/kernel/kern_ctx_esp32.h` | CREATE | 汇编函数的 C 声明 |
| `src/app/ui_task.c` | UPDATE | 移除 FreeRTOS 自旋锁注释（已是单线程） |

### Phase 3: procfs 完善 + 任务化

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_procfs.c` | UPDATE | uptime 接入 tick 计数器，新增 /proc/meminfo |
| `src/kernel/kern_task.c` | UPDATE | 动态栈扩展 realloc 逻辑 |
| `src/app/wifi/wifi_manager.cpp` | UPDATE | 包装为 `wifi_mgr_task_main`，改造为可独立 spawn |
| `src/app/bluetooth/bt_manager.cpp` | UPDATE | 同上 |
| `src/app/ui_task.c` | UPDATE | 通过 IPC 接收 WiFi/BT 状态变更，移除 _update() 直接调用 |

### 串口监视器 Bug 修复（附带）

| File | Action | Why |
|---|---|---|
| `src/ui/ui_item.c` | EDIT | `xerintosh_new_user_item()` 初始化 5 个派生 bool |
| `src/ui/ui_core.c` | EDIT | 退出后重置 `exiting_user_item` |
| `src/app/serial_monitor/sm_ui.c` | EDIT | term_y 间距修复文字边框重合 |

### 工具

| File | Action | Why |
|---|---|---|
| `tools/verify_serial.py` | CREATE | 自动化串口验证脚本（pyserial） |
| `tools/serial_mon.py` | CREATE | 交互式串口监视器（Python） |

---

## Tasks

### Phase 0: Shell 命令解析器重构 + 新增命令

#### Task 0.1: 命令行解析器 (`kern_shell_parser.c`)

- **Action**: 实现 `shell_tokenize(line, tokens, max_tokens)` 函数
  - 跳过前导空白
  - 引号支持：`"hello world"` 作为一个 token
  - 转义字符：`\n`, `\t`, `\\`, `\"`
  - 返回 token 数量，-1 表示引号未闭合
- **Mirror**: Linux shell 行为，最小实现
- **Validate**: 编译通过 + native 单元测试

#### Task 0.2: 命令注册表 + 精确匹配

- **Action**: 
  - 定义 `shell_cmd_t` 结构体：`{name, handler, help_text}`
  - 静态命令表替换 `strncmp` 链为 `for` 循环精确匹配
  - 支持 `kern_shell_register_cmd()` 动态注册（供后续扩展）
- **Validate**: 串口输入错误命令 → 友好提示 "command not found: xxx"

#### Task 0.3: 新增 8 个命令

| 命令 | 功能 | 实现要点 |
|------|------|---------|
| `free` | 堆内存使用 | `ESP.getFreeHeap()` / `ESP.getMinFreeHeap()` |
| `kill <pid>` | 终止任务 | 遍历任务列表，标记 ZOMBIE |
| `uname` | 系统信息 | 内核版本 + 编译时间 (`__DATE__ __TIME__`) + 平台 |
| `df` | VFS 使用统计 | dentry 总数、inode 总数、打开 fd 数 |
| `clear` | 清屏 | 发送 ANSI `\x1b[2J\x1b[H` |
| `history` | 命令历史 | 最多 16 条环形缓冲区 |
| `date` | 运行时间 | `millis()` 转换为可读格式 |
| `hexdump <path>` | 十六进制查看 | 类 `xxd` 输出，每行 16 字节 |

- **Validate**: 每个命令单独串口验证

#### Task 0.4: 命令历史

- **Action**: 环形缓冲区 16 条，↑ 浏览上一条，↓ 浏览下一条
- **Note**: 需要解析 VT100 转义序列 (`\x1b[A` = 上箭头)
- **Validate**: 串口输入几条命令后按 ↑ 能回溯

#### Task 0.5: Native 单元测试

- **Action**: 新增 `test/test_native/test_shell_parser.cpp`
  - tokenize 基本用例
  - 引号/转义边界用例
  - 命令匹配精度用例
- **Validate**: `pio test -e native --gtest_filter=ShellParserTest.*`

---

### Phase 1: sysfs ↔ 硬件双向绑定

#### Task 1.1: sysfs 动态绑定 API

- **Action**: `kern_sysfs_bind(attr, value_ptr, on_change_cb)`
  - 替换 `kern_sysfs.c` 中静态 `g_sysfs_attrs` 表的 `value_ptr`
  - write 后自动调用 `on_change_cb`
- **Validate**: Shell 执行 `echo 128 > /sys/brightness` → 屏幕亮度改变

#### Task 1.2: 接入实际硬件变量

- **Action**: `main.cpp` 中 `deferred_kernel_init()` 后调用：
  ```c
  kern_sysfs_bind(KERN_SYSFS_BRIGHTNESS, &g_brightness_level, on_brightness_change_cb);
  kern_sysfs_bind(KERN_SYSFS_ROTATION, &g_screen_rotation_level, on_screen_rotation_change_cb);
  kern_sysfs_bind(KERN_SYSFS_ANIM_SPEED, &g_anim_speed, on_anim_speed_change_cb);
  kern_sysfs_bind(KERN_SYSFS_ANIM_ENABLED, (int32_t*)&g_anim_enabled, on_anim_enabled_change_cb);
  kern_sysfs_bind(KERN_SYSFS_LOG_LEVEL, ..., ...);
  ```
- **Validate**: 每个 sysfs 属性读写均能触发硬件变化

---

### Phase 2: ESP32 调度器去 FreeRTOS 化

#### Task 2.1: Xtensa 上下文切换原语 (`kern_ctx_esp32.S`)

- **Action**: 实现 `kern_ctx_make(ctx, stack_top, entry, arg)` 汇编函数
  ```asm
  .section .text
  .globl kern_ctx_make
  .type kern_ctx_make, @function
  kern_ctx_make:
      mov a1, a3        // SP = stack_top (arg2)
      mov a3, a5        // 重排参数
      jx  _kern_task_bootstrap
  ```
- **C 蹦床** `_kern_task_bootstrap(ctx, entry, arg)`:
  ```c
  __attribute__((noinline))
  void _kern_task_bootstrap(kern_ctx_t *ctx, void (*entry)(void*), void *arg) {
      if (setjmp(*ctx) == 0) {
          entry(arg);
          kern_exit();     // entry 返回 → 标记 ZOMBIE → longjmp 回调度器
      }
      // longjmp 回来时直接返回（实际不会走到这里）
  }
  ```
- **Validate**: `pio run -e m5stick-c` 编译通过

#### Task 2.2: 重构 `kern_task.c` ESP32 分支

- **Action**: 逐函数替换：
  - `kern_sched_init()`: `setjmp(g_sched_ctx)` → idle TCB 复用主栈
  - `kern_sched_tick()`: `pick_next_ready()` → `longjmp(next->ctx, 1)`
  - `kern_spawn()`: malloc TCB + 堆栈 → `kern_ctx_make()`
  - `kern_yield()`: `setjmp(current->ctx)` → `longjmp(g_sched_ctx, 1)`
  - `kern_exit()`: 标记 ZOMBIE → `longjmp(g_sched_ctx, 1)`
  - `kern_sleep_ms()`: 设置 `wake_time` → yield
- **Mirror**: 逐函数对齐 native 端 `ucontext` 实现的语义
- **Validate**: 硬件上 UI 正常导航、Shell 正常交互、`ps` 命令正确

#### Task 2.3: 移除 FreeRTOS 依赖残留

- **Action**: 
  - `kern_task.h`: `#include <freertos/...>` → `#include <setjmp.h>`
  - `kern_task.h`: `typedef TaskHandle_t kern_ctx_t` → `typedef jmp_buf kern_ctx_t`
  - 确认 `xTaskCreate`/`vTaskDelete`/`vTaskDelay`/`taskYIELD` 零调用
- **Validate**: `grep -r "xTaskCreate\|vTaskDelay" src/kernel/` 无输出

---

### Phase 3: procfs 完善 + 任务化解耦

#### Task 3.1: procfs uptime 接入

- **Action**: `kern_task.c` 新增 `g_sched_ticks` 全局计数器，每次 `kern_sched_tick()` 递增
  - `procfs_uptime_generate()` 读取 `g_sched_ticks`，按 tick 周期转换
- **Validate**: Shell `cat /proc/uptime` 返回非零递增数值

#### Task 3.2: procfs meminfo

- **Action**: 新增 `/proc/meminfo`：`ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, `ESP.getHeapSize()`
- **Validate**: Shell `cat /proc/meminfo` 返回内存统计

#### Task 3.3: 动态栈扩展

- **Action**: `kern_yield()` 中检查栈使用率 (`sp - stack_base > stack_size * 0.8`)
  - `realloc` 扩展 1KB（上限 8KB）
  - memcpy 旧栈内容到新地址
  - 更新 TCB 中 `stack_base` / `stack_size`
- **Validate**: 递归测试任务触发自动扩展且数据不丢失

#### Task 3.4: WiFi/BT 独立任务化

- **Action**:
  1. WiFi 管理器改造：`wifi_mgr_task_main()` 包装 `wifi_mgr_update()` + `kern_yield()`
  2. BT 管理器同理
  3. 状态变更通过 `kern_mq_send()` 发消息给 UI 任务
  4. `ui_task_main()` 移除 `wifi_mgr_update()`/`bt_mgr_update()` 直接调用
  5. 改为 `kern_mq_recv()` 读取消息后按需重建菜单
- **Validate**: WiFi 扫描时 UI 按键仍能响应（不再卡顿）

---

## Hardware Verification Protocol

### 开发环境

- **串口设备**: `/dev/cu.usbserial-4D52671EFA`
- **波特率**: 115200 (可在设置中修改)
- **PlatformIO 路径**: `~/.platformio/penv/bin/pio`

### 自动化验证脚本

`tools/verify_serial.py` — 烧录后自动验证：

```python
#!/usr/bin/env python3
"""Xeros 内核串口验证脚本
用法: python3 tools/verify_serial.py [--test phase_name]
"""
import serial
import time
import sys

SERIAL_PORT = "/dev/cu.usbserial-4D52671EFA"
BAUD = 115200
TIMEOUT = 3.0

def wait_for_prompt(ser, timeout=10.0):
    """等待 Shell 提示符出现"""
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            print(chunk.decode(errors="replace"), end="", flush=True)
            if b"$ " in buf:
                return True
    return False

def send_cmd(ser, cmd, wait=1.0):
    """发送命令并等待响应"""
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    output = ser.read(ser.in_waiting or 1024)
    text = output.decode(errors="replace")
    print(text, end="", flush=True)
    return text

def test_shell_basic(ser):
    """验证 Shell 基本命令"""
    results = []
    
    # help
    send_cmd(ser, "help")
    
    # ps
    out = send_cmd(ser, "ps")
    results.append(("ps shows tasks", "PID" in out and "ui" in out))
    
    # ls /
    out = send_cmd(ser, "ls /")
    results.append(("ls / shows filesystems", "dev" in out and "proc" in out))
    
    # pwd
    send_cmd(ser, "pwd")
    
    # cat /proc/version
    out = send_cmd(ser, "cat /proc/version")
    results.append(("cat /proc/version", "Xeros" in out))
    
    # free (new)
    out = send_cmd(ser, "free")
    results.append(("free shows heap", "free_heap" in out.lower() or "bytes" in out.lower()))
    
    # uname (new)
    out = send_cmd(ser, "uname")
    results.append(("uname works", "Xeros" in out))
    
    return results

def test_sysfs(ser):
    """验证 sysfs 读写"""
    results = []
    
    # 读取亮度
    out = send_cmd(ser, "cat /sys/brightness")
    results.append(("read brightness", out.strip().isdigit()))
    
    # 写入亮度 (有效值)
    send_cmd(ser, "echo 200 > /sys/brightness")
    out = send_cmd(ser, "cat /sys/brightness")
    results.append(("write brightness", "200" in out))
    
    # 写入无效值
    send_cmd(ser, "echo 999 > /sys/brightness")
    out = send_cmd(ser, "cat /sys/brightness")
    results.append(("invalid write rejected", "200" in out))  # 应保持原值
    
    return results

def main():
    phase = sys.argv[2] if len(sys.argv) > 2 and sys.argv[1] == "--test" else "basic"
    
    print(f"[VERIFY] Opening {SERIAL_PORT} @ {BAUD}...")
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=TIMEOUT)
    time.sleep(2)
    ser.reset_input_buffer()
    
    print("[VERIFY] Waiting for Xeros Shell prompt...")
    if not wait_for_prompt(ser):
        print("[FAIL] No shell prompt detected")
        ser.close()
        sys.exit(1)
    
    # 发送空行获取提示符
    send_cmd(ser, "")
    
    if phase in ("basic", "shell", "phase0"):
        results = test_shell_basic(ser)
    elif phase in ("sysfs", "phase1"):
        results = test_sysfs(ser)
    else:
        results = test_shell_basic(ser) + test_sysfs(ser)
    
    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"\n[VERIFY] Results: {passed}/{total} passed")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    
    ser.close()
    sys.exit(0 if passed == total else 1)

if __name__ == "__main__":
    main()
```

### 交互式串口监视器

`tools/serial_mon.py` — 开发调试用：

```python
#!/usr/bin/env python3
"""M5Stick-P1 交互式串口监视器"""
import serial
import sys
import threading

PORT = "/dev/cu.usbserial-4D52671EFA"
BAUD = 115200

def reader(ser):
    while True:
        data = ser.read(ser.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    t = threading.Thread(target=reader, args=(ser,), daemon=True)
    t.start()
    print(f"Connected to {PORT} @ {BAUD}. Ctrl+] to quit.")
    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                break
            ser.write(line.encode())
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

if __name__ == "__main__":
    main()
```

---

## Validation

### 编译验证

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"

# 每阶段编译检查
pio run -e m5stick-c
pio run -e native
```

### 单元测试

```bash
# Native 测试
pio test -e native
./.pio/build/native/program --gtest_filter=ShellParserTest.*
./.pio/build/native/program --gtest_filter=KernelSyscallTest.*
./.pio/build/native/program --gtest_filter=KernelTaskTest.*
```

### 硬件验证

```bash
# 烧录
pio run -e m5stick-c --target upload

# 自动化验证
python3 tools/verify_serial.py --test phase0
python3 tools/verify_serial.py --test phase1

# 交互式调试
python3 tools/serial_mon.py
```

### 每阶段验收标准

| Phase | 验收条件 |
|-------|---------|
| 0 | `help`/`ps`/`ls`/`pwd` 正常；`free`/`uname`/`df`/`clear`/`history`/`date`/`hexdump` 新增命令可用；`kill` 可终止任务 |
| 1 | `cat /sys/brightness` 返回实际亮度；`echo 128 > /sys/brightness` 屏幕亮度改变 |
| 2 | UI 菜单正常导航；`ps` 显示协作式状态；`kern_yield()` 行为与 native 一致 |
| 3 | `cat /proc/uptime` 返回非零递增数值；`cat /proc/meminfo` 返回内存统计；WiFi 扫描时 UI 不卡顿 |

---

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| **`jmp_buf` 在 Xtensa 上不完整保存协程上下文** | MEDIUM | ESP-IDF newlib 的 `setjmp`/`longjmp` 是 FreeRTOS 上下文切换基础，久经考验 |
| **栈切换汇编与编译器 ABI 不兼容** | MEDIUM | 蹦床函数用 `__attribute__((noinline))` + `register` 变量确保参数在寄存器中 |
| **sysfs 绑定后 Shell 修改亮度与 UI 菜单冲突** | LOW | 两者操作同一变量，最后一次写入生效；UI 菜单刷新时同步最新值 |
| **WiFi/BT 独立任务化后 IPC 消息丢失** | MEDIUM | MQ 有 `msg_count` 上限检测，UI 任务定期轮询而非阻塞等待 |
| **动态栈 realloc 导致数据丢失** | HIGH | 栈内仅存局部变量和返回地址，扩展时 memcpy 到新地址；TCB 中仅保存栈基址 |

---

## Acceptance Criteria

### Phase 0 (Shell)
- [ ] 所有原有命令（ls/cd/pwd/ps/cat/cp/rm/mkdir/touch/echo/reboot/help）行为不变
- [ ] `free` 命令返回堆内存统计
- [ ] `uname` 命令返回系统信息
- [ ] `kill <pid>` 可终止任务（`ps` 确认状态变为 ZOMBIE）
- [ ] `df` 命令返回 VFS 使用统计
- [ ] `clear` 命令清屏
- [ ] `history` 命令显示历史 + ↑↓ 浏览
- [ ] `date` 命令返回运行时间
- [ ] `hexdump <path>` 以十六进制显示文件内容
- [ ] 命令解析器正确处理引号和转义
- [ ] 错误命令给出 "command not found" 提示
- [ ] Native 测试全部通过

### Phase 1 (sysfs)
- [ ] `cat /sys/brightness` 返回与 UI 菜单一致的亮度值
- [ ] `echo 128 > /sys/brightness` 屏幕亮度立即改变
- [ ] 其他 sysfs 属性同理

### Phase 2 (去 FreeRTOS)
- [ ] `kern_task.c` ESP32 分支不再调用 `xTaskCreate`/`vTaskDelete`/`vTaskDelay`
- [ ] UI 菜单正常响应按键
- [ ] Shell 正常交互
- [ ] `ps` 命令显示正确的协作式任务状态
- [ ] 栈 canary 溢出检测正常工作

### Phase 3 (procfs + 任务化)
- [ ] `/proc/uptime` 返回非零递增数值
- [ ] `/proc/meminfo` 返回内存统计
- [ ] 动态栈扩展触发时数据不丢失
- [ ] WiFi 扫描期间 UI 按键响应不卡顿

---

## Note: 串口监视器 Bug 修复（附带）

已定位的 3 个 Bug（详见 `serial-monitor-3-bugs.plan.md`）将在 Phase 0 开始前作为前置修复，改动极小（3 个文件共约 8 行），解决：

- 选择「串口监视器」即触发进入（`in_user_item` 垃圾值）
- 终端文字与边框重合（`term_y` 间距）
- 首次进入时长按无效（连锁反应）

---

## Reference: 完整内核文件清单

```
src/kernel/
├── kern_types.h          # 类型系统、错误码、常量
├── kern_init.h/c         # 内核初始化、日志、panic
├── kern_task.h/c         # 任务控制块、协作式调度器
├── kern_vfs.h/c          # 虚拟文件系统
├── kern_devfs.h/c        # 设备文件系统
├── kern_procfs.h/c       # /proc 进程信息 FS
├── kern_sysfs.h/c        # /sys 系统配置 FS
├── kern_ipc.h/c          # Pipe + Message Queue
├── kern_syscall.h/c      # 系统调用分发
├── kern_shell.h/c        # ★ 串口 Shell（本次主要改动）
├── kern_shell_parser.h/c # ★ 新增：命令解析器
├── kern_shell_cmds.h/c   # ★ 新增：扩展命令
├── kern_ctx_esp32.S      # ★ 新增：Xtensa 上下文切换
├── kern_ctx_esp32.h      # ★ 新增：汇编函数声明
├── debug_serial.h/cpp    # 调试串口输出
└── devices/
    ├── kern_devices.h/c  # 设备初始化汇总
    ├── dev_ttyS0.h/cpp   # /dev/ttyS0 串口设备
    ├── dev_fb0.h/c       # /dev/fb0 帧缓冲
    ├── dev_input0.h/c    # /dev/input0 按键
    └── dev_null.h/c      # /dev/null
```
