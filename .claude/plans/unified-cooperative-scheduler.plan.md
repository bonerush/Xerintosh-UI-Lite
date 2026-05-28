# Plan: 统一协作式调度器（去除 ESP32 端 FreeRTOS 任务依赖）

**Source PRD**: 用户需求（对话式）
**Complexity**: Large

## Summary

当前 `kern_task.c` 有两套完全不同的调度实现：
- **Native**：POSIX `ucontext`/`swapcontext`/`makecontext` 纯协作式调度
- **ESP32**：`xTaskCreate`/`vTaskDelay`/`vTaskDelete` 抢占式（FreeRTOS 管理一切）

这导致 `kern_yield()` 在 ESP32 上只是 `vTaskDelay(1)`（并非真正的协作式切换），调度行为与 native 不一致。目标是用 **`setjmp`/`longjmp` + 手动栈管理** 替换 ESP32 端的 FreeRTOS 任务 API，实现与 native 端行为一致的纯协作式 Round-Robin 调度器。

**FreeRTOS 继续在底层为 WiFi/BT 协议栈服务（这是必须的——ESP32 Arduino 的 WiFi/BT 库深度依赖 FreeRTOS），Xeros 内核任务运行在 Arduino `loop()` 的单一线程内，不创建新的 FreeRTOS 任务。**

---

## Architecture Change

```
Before (ESP32):
  Arduino loop() → kern_sched_tick() [no-op]
  FreeRTOS task "ui"     → xTaskCreate → freertos_task_wrapper → ui_task_main
  FreeRTOS task "shell"  → xTaskCreate → freertos_task_wrapper → shell_task_main
  FreeRTOS task "idle"   → xTaskCreate → freertos_task_wrapper → idle_entry

After (ESP32):
  Arduino loop() → kern_sched_tick() → swapcontext-like dispatch
  ┌─────────────────────────────────────────────────┐
  │ Single-threaded cooperative scheduler (loop())   │
  │  task "ui"    ─→ setjmp/longjmp ─→ ui_task_main │
  │  task "shell" ─→ setjmp/longjmp ─→ shell_task   │
  │  task "idle"  ─→ setjmp/longjmp ─→ idle_entry   │
  │  task "wifi"  ─→ setjmp/longjmp ─→ wifi_task    │
  │  task "bt"    ─→ setjmp/longjmp ─→ bt_task      │
  └─────────────────────────────────────────────────┘

  FreeRTOS (底层，不动):
    - WiFi 协议栈 (esp_wifi)
    - NimBLE 协议栈
    - M5Unified 内部定时器
    - Arduino loop() 任务本身
```

---

## Patterns to Mirror

| Category | Source | Pattern |
|---|---|---|
| Naming | `kern_task.c:94` | `kern_ctx_t` typedef + `#ifdef` 平台选择 |
| Context type | native `ucontext_t` | ESP32 用 `jmp_buf`（`<setjmp.h>`）替代 |
| Error handling | `kern_task.c:72` | 分配失败 → `kern_panic()` |
| Stack management | `kern_task.c:576-589` | 堆分配 + canary + 0xAA 填充 |
| Scheduling | `kern_task.c:548-573` | `pick_next_ready()` Round-Robin + sleep 唤醒 |
| Callbacks | `kern_task.c:44` | `void (*entry)(void *arg)` 统一签名 |

---

## Files to Change

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_task.h` | UPDATE | `kern_ctx_t` 在 ESP32 改为 `jmp_buf`；移除 `#include <freertos/...>` |
| `src/kernel/kern_task.c` | UPDATE | 核心改动——替换全部 `#else`（ESP32）分支为 `setjmp`/`longjmp` 实现 |
| `src/kernel/kern_ctx_esp32.S` | CREATE | Xtensa 汇编：初始任务栈切换（`kern_ctx_make`） |
| `src/kernel/kern_ctx_esp32.h` | CREATE | 汇编函数的 C 声明 |
| `src/kernel/kern_shell.c` | UPDATE | 移除 `#include <Arduino.h>`（仅 ESP32 时需要）— 保持不变 |
| `src/main.cpp` | UPDATE | `loop()` 中 `dev_ttyS0_poll()` 保持（需 FreeRTOS 上下文读取串口硬件） |
| `src/app/ui_task.c` | UPDATE | 移除 FreeRTOS 自旋锁 `UI_LOCK/UI_UNLOCK`（单线程无竞争） |
| `test/test_native/test_kernel_syscall.cpp` | UPDATE | 增加多任务协作式调度测试 |

---

## Tasks

### Task 1: Xtensa 上下文切换原语（`kern_ctx_esp32.S`）

- **Action**: 实现 `kern_ctx_make(ctx, stack_top, entry, arg)` 汇编函数
  - 切换到目标任务栈：`mov a1, a3`（a3 = stack_top）
  - 调用 C 蹦床函数 `_kern_task_bootstrap(ctx, entry, arg)`
  - 蹦床函数在任务栈上调用 `setjmp(ctx)` 捕获初始上下文（此时 SP 正确）
  - 然后调用 `entry(arg)` 执行任务主函数
  - entry 返回后标记 ZOMBIE 并 `longjmp(g_sched_ctx, 1)` 回到调度器
- **Mirror**: native 端 `makecontext` + `swapcontext` 的行为
- **Validate**: 编译通过 `pio run -e m5stick-c`

### Task 2: 重构 ESP32 调度器核心（`kern_task.c`）

- **Action**: 替换全部 `#else`（ESP32 FreeRTOS）分支为 `setjmp`/`longjmp` 实现：
  - `kern_sched_init()`: 创建 idle TCB，idle 任务复用主栈（不分配新栈）
  - `kern_sched_tick()`: 同 native——`setjmp(g_sched_ctx)` 保存调度器上下文，`longjmp(next->ctx, 1)` 切换到目标任务
  - `kern_spawn()`: 分配 TCB + 堆栈 + `kern_ctx_make()` 设置初始上下文
  - `kern_yield()`: `setjmp(current->ctx)` 保存当前 → `longjmp(g_sched_ctx, 1)` 回到调度器
  - `kern_exit()`: 标记 ZOMBIE → `longjmp(g_sched_ctx, 1)` 回到调度器
  - `kern_sleep_ms()`: 设置 `wake_time` → `setjmp(current->ctx)` → `longjmp(g_sched_ctx, 1)`
  - `kern_task_stack_usage()`: 扫描栈上 0xAA 模式 + canary 校验（同 native）
- **Mirror**: 逐函数对齐 native 端实现的行为
- **Validate**: `pio run -e m5stick-c` 编译通过

### Task 3: 移除 FreeRTOS 依赖

- **Action**:
  - `kern_task.h` 中 ESP32 的 `#include <freertos/...>` → 改为 `#include <setjmp.h>`
  - `kern_task.h` 中 `typedef TaskHandle_t kern_ctx_t` → 改为 `typedef jmp_buf kern_ctx_t`
  - `ui_task.c` 中 `portENTER_CRITICAL/portEXIT_CRITICAL` → 直接移除（单线程无竞争）
- **Validate**: `pio run -e m5stick-c` 编译通过，无 FreeRTOS 任务 API 调用残留

### Task 4: 集成测试与验证

- **Action**:
  - 硬件上创建 3 个测试任务，验证交替打印计数器
  - 验证 `kern_sleep_ms()` 正确唤醒任务
  - 验证栈 canary 溢出检测
  - 验证 `ps` shell 命令正确显示任务状态（READY/RUNNING/SLEEPING/ZOMBIE）
  - Native 测试增加多任务协作式调度测试
- **Validate**: 硬件环境 `pio run -e m5stick-c --target upload` + 串口验证；native `pio test -e native`

---

## Key Design Decisions

### 1. 为什么用 `setjmp`/`longjmp` 而不是 `ucontext`？

ESP32 的 newlib **不提供** `<ucontext.h>`（它是 POSIX 2001 的可选特性，Xtensa 移植未实现）。`setjmp`/`longjmp` 在 ESP-IDF newlib 中可用且经过充分测试。

### 2. 为什么 idle 任务复用主栈？

idle 任务只做 `while(1) kern_yield()`，不会深层嵌套调用。复用 Arduino `loop()` 的栈（FreeRTOS 分配的 ~8KB）避免额外堆分配。其他任务的堆栈在堆上分配。

### 3. 为什么需要汇编 `kern_ctx_make`？

纯 C 无法安全地切换栈指针（SP）。需要约 5 条 Xtensa 汇编指令：
```asm
mov a1, a3       // SP = stack_top (arg2)
mov a3, a5       // 重排参数: a2=ctx, a3=entry, a4=arg
jx  _kern_task_bootstrap  // 跳转到蹦床，不设置返回地址
```
蹦床函数 `_kern_task_bootstrap` 在**新栈上**执行 `setjmp()`，从而捕获正确的 SP 到 `jmp_buf`。

### 4. WiFi/BT 协议栈依赖 FreeRTOS，如何兼容？

ESP32 Arduino 的 WiFi/BT 协议栈在 FreeRTOS 任务中运行（不受影响）。Xeros 任务运行在 Arduino `loop()` 的单线程内。WiFi/BT 管理器的 `_update()` 函数在每个 Xeros tick 之间被调用（通过 `ui_task_main` 中的 `wifi_mgr_update()` / `bt_mgr_update()`），或者后续拆分为独立的 Xeros 任务。

### 5. 统一 `kern_ctx_t` 的类型定义

```c
#ifdef NATIVE_TEST
#include <ucontext.h>
typedef ucontext_t kern_ctx_t;
#else
#include <setjmp.h>
typedef jmp_buf kern_ctx_t;
#endif
```

`jmp_buf` 在 Xtensa 上典型大小为 24 字节（保存 a0/PC、a1/SP、a12-a15），远小于 `ucontext_t`（约 936 字节），更适合嵌入式场景。

---

## Validation

```bash
# 编译检查
pio run -e native          # native 编译
pio run -e m5stick-c       # ESP32 编译

# Native 测试
pio test -e native --gtest_filter=KernelSyscallTest.*

# 硬件验证
pio run -e m5stick-c --target upload
pio device monitor -e m5stick-c
# 串口输入: ps → 验证任务列表
# 验证: 菜单按键响应正常、shell 正常工作
```

---

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| **`jmp_buf` 大小/布局不稳定** | LOW | 使用标准 `<setjmp.h>` + `sizeof(jmp_buf)`，不手动构造 jmp_buf 内容 |
| **Xtensa 窗口寄存器 `longjmp` 不完全恢复** | LOW | ESP-IDF newlib 的 `setjmp`/`longjmp` 是 FreeRTOS 上下文切换的基础，久经考验 |
| **栈切换汇编与编译器 ABI 不兼容** | MEDIUM | 蹦床函数用 `__attribute__((noinline))` + `register` 变量确保参数在寄存器中 |
| **`kern_sleep_ms` 时间精度下降** | LOW | ESP32 `loop()` 帧率 ~30-60fps，睡眠精度约 16-33ms，对 UI 任务足够 |
| **崩溃时难以调试** | MEDIUM | `kern_panic()` 在 panic 时打印任务列表和栈使用量 |

---

## Acceptance Criteria

- [ ] ESP32 端 `kern_task.c` 不再调用任何 FreeRTOS 任务 API（`xTaskCreate`/`vTaskDelete`/`vTaskDelay`/`taskYIELD`）
- [ ] `kern_yield()` / `kern_sleep_ms()` / `kern_exit()` 行为与 native 端一致
- [ ] 硬件上 UI 菜单正常导航、shell 正常交互
- [ ] `ps` 命令显示所有任务及其正确的协作式状态
- [ ] 栈 canary 溢出检测正常工作
- [ ] 所有新文件 < 400 行，函数 < 50 行
- [ ] 现有 native 测试全部通过（无回归）
