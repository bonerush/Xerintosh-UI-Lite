# Xeros 原生内核调试日志

> **Parent:** [项目索引](../index.md) | **Branch:** `xeros-native-bringup` | **Start:** 2026-06-26

本文档记录 Xeros 原生内核（XEROS_NATIVE_SCHED）从 FreeRTOS 后端迁移到完全自主内核的全过程，包括调查研究、崩溃分析、根因定位与修复验证。

---

## 调查阶段：为什么 FreeRTOS 能在 M5Stick-C 上稳定运行

### 1. 硬件与构建环境

- **硬件**: M5Stick-C (ESP32-PICO)
- **框架**: ESP-IDF + PlatformIO
- **默认后端**: FreeRTOS 任务容器 + 双信号量令牌协议
- **原生环境**: `m5stick-c-native`（定义 `-D XEROS_NATIVE_SCHED`，但目前仍 fallback 到 FreeRTOS）

### 2. FreeRTOS 稳定运行的关键机制

#### 2.1 看门狗喂狗（FreeRTOS 后端时期）

在 FreeRTOS 双信号量后端中，`src/main.cpp` 的调度循环显式调用：

```cpp
vTaskDelay(pdMS_TO_TICKS(1));
```

*📄 Source: [main.cpp](../src/main.cpp#L283)（Phase 2A 已替换为 `kern_port_idle()`）*

**原因**: Xeros 调度器任务优先级高于 FreeRTOS idle（0）。若调度器长时间不 yield，idle 任务无法在 300ms 内喂 INT_WDT，导致复位。`vTaskDelay(1)` 强制让出 CPU 给 idle。

`sdkconfig.m5stick-c-native` 中已关闭 `CONFIG_ESP_INT_WDT`，为原生调度器实验留出空间；Phase 2A 进一步移除了 Xeros 路径对 `vTaskDelay` 的显式依赖。

#### 2.2 优先级分层

| 优先级 | 任务 | 作用 |
|--------|------|------|
| +2 | UI (`ui`) | 保证渲染帧不被 1ms tick 打断 |
| +1 | `app_main` / 普通 Xeros 任务 | 调度器大部分时间阻塞在信号量上 |
| 0 | `xidleN` / FreeRTOS idle | 喂 INT_WDT、兜底运行 |

*📄 Source: [kern_port_freertos.c](../src/kernel/kern_port_freertos.c#L248-L263)*

#### 2.3 中断最小化

GPTimer ISR 仅设置标志：

```c
static bool IRAM_ATTR sched_timer_isr(...) {
    g_preempt_tick_pending = true;
    return false;
}
```

*📄 Source: [kern_port_freertos.c](../src/kernel/kern_port_freertos.c#L66-L73)*

所有调度逻辑在 `app_main` loop 任务上下文执行，避免 ISR 中调用阻塞 API。

#### 2.4 双信号量令牌协议

调度器与每个 Xeros 任务之间通过 `g_token_sem[cpu]` 和 `g_done_sem[cpu]` 交互：

```
Scheduler: give(token)  → Task wakes
Task:      give(done)   → Scheduler resumes
```

*📄 Source: [kern_port_freertos.c](../src/kernel/kern_port_freertos.c#L315-L368)*

这使得 Xeros 可以使用 FreeRTOS 的任务容器和栈管理，同时保留自己的调度策略。

### 3. XEROS_NATIVE_SCHED 当前状态

- `ctx_switch_native.S` 中仅保留新任务首次运行的汇编启动桩 `xeros_task_start_asm`。
- 上下文保存/恢复已改为复用 newlib 的 `setjmp`/`longjmp`（见 `ctx_init.c`）。
- TCB 已添加 `native_ctx` / `native_stack` / `native_ctx_valid` 字段。
- `m5stick-c-native` 现在走 `kern_port_esp32_native.c` 原生后端，不再 fallback 到 FreeRTOS 双信号量协议。

### 4. 历史失败教训（call8 ABI）

`doc/debug-watchdog-crash.md` 记录了之前原生上下文的尝试：

- **call8 + entry 双重窗口旋转**导致寄存器映射错误。
- **CALLINC 设置错误**（`0x00040023` vs `0x00050023`）。
- **WINDOWSTART 掩码不足**导致窗口下溢异常。
- **`rsil` 特权指令**在用户模式（PS.UM=1）下触发异常。
- **`xthal_window_spill_nw`** 依赖正常 entry 调用链，在上下文切换中不安全。

**结论**: 与其继续 debug call8 方案，不如仿照 FreeRTOS 使用 **call0 ABI 调度器 + windowed ABI 任务**。

---

## 参考代码

- FreeRTOS Xtensa port: `ref/esp-idf/components/freertos/FreeRTOS-Kernel/portable/xtensa/`
- 关键文件：
  - `portasm.S`
  - `port.c`
  - `include/xtensa_context.h`
  - `portmacro.h`

---

## 调试工具

使用 `tools/xeros_debug.py`：

```bash
# 复位并等待启动
python3 tools/xeros_debug.py /dev/ttyUSB0 --reset --wait-boot

# 监控日志
python3 tools/xeros_debug.py /dev/ttyUSB0 --monitor --log debug.log

# 发送命令
python3 tools/xeros_debug.py /dev/ttyUSB0 --cmd "ps"
```

---

## Phase 1 实施记录

### 2026-06-26：切换到 setjmp/longjmp 方案

#### 决策

在尝试手写 call0/windowed 混合汇编上下文切换时，遇到 `xeros_ctx_restore` 中手动 `retw` 恢复失败的问题：

- `retw` 依赖被恢复任务栈帧中的保存区来重建窗口下溢链；
- 手动填充的 `a0/a1` 与 `WINDOWBASE/WINDOWSTART` 无法让硬件下溢处理器找到正确的调用者寄存器；
- 设备在 `blink count=0` 后静默挂起，`xeros_ctx_restore_call0` 进入但调度器无法恢复。

因此决定**复用 newlib 的 `setjmp`/`longjmp`**。newlib 的实现使用 `syscall` 刷写全部寄存器窗口，并用 `retw` 从正确建立的栈帧中恢复，已被工具链验证。

*📄 Source: [ctx_switch.h](../src/kernel/esp32/ctx_switch.h#L24-L32)*

#### 关键改动

1. **上下文类型改为 `jmp_buf`**

   `kern_ctx_native_t` 不再使用自定义结构体，而是直接 `typedef jmp_buf kern_ctx_native_t`。

   *📄 Source: [ctx_switch.h](../src/kernel/esp32/ctx_switch.h#L30)*

2. **保存/恢复使用 `setjmp`/`longjmp`**

   ```c
   int xeros_ctx_save(kern_ctx_native_t *ctx) { return setjmp(*ctx); }
   void xeros_ctx_restore(kern_ctx_native_t *ctx) { longjmp(*ctx, 1); }
   ```

   *📄 Source: [ctx_init.c](../src/kernel/esp32/ctx_init.c#L26-L33)*

3. **新任务首次运行走专用汇编启动桩**

   第一次切换到任务时不能 `longjmp`（还没有 `setjmp` 建立的上下文）。`xeros_task_start` 计算栈顶，交给汇编桩 `xeros_task_start_asm` 切换 SP，然后以 `call8` 调用 `xeros_task_wrapper(entry, arg)`。

   **call8 参数传递注意**：调用者的 `a10/a11` 会成为被调函数的 `a2/a3`，因此启动桩把参数放到 `a10/a11`。

   *📄 Source: [ctx_switch_native.S](../src/kernel/esp32/ctx_switch_native.S#L33-L52)*
   *📄 Source: [ctx_init.c](../src/kernel/esp32/ctx_init.c#L50-L72)*

4. **TCB 增加 `native_ctx_valid` 标志**

   标记任务上下文是否已通过 `setjmp` 建立。首次运行后由 `native_switch_to` 置位，后续切换直接使用 `longjmp`。

   *📄 Source: [kern_task.h](../src/kernel/kern_task.h#L64-L67)*

5. **后端 `kern_port_esp32_native.c` 适配**

   `native_switch_to` 区分首次启动与已建立上下文两种路径；`native_task_yield` / `native_task_exit` 直接使用 `xeros_ctx_save` / `xeros_ctx_restore`。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L86-L122)*

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 默认环境编译 | `pio run -e m5stick-c` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| 5 分钟稳定性测试 | 串口抓取 300s | **仅 1 次上电复位，0 次 Guru Meditation，blink 计数持续增长** |

串口输出示例：

```
[native] blink task spawned (pid=2)
[native] switch_to: blink (state=1)
[native] switch_to save ret=0
[native] task_wrapper: pxCode=0x400d49c4
[native] blink_task started
[native] blink count=0
[native] task_yield: saving blink
[native] task_yield: save done, restoring sched
[native] switch_to save ret=1
[native] switch_to resumed
...
[native] blink count=12534
```

#### 已知问题

- `pio test -e native` 中 `test_ctx_switch.cpp` 已随 `kern_ctx_native_t` 改为 `jmp_buf` 同步更新，但仍有 `dev_ttyS0.cpp` 的 `g_ttyS0_mux` 和 `test_kernel_sync.cpp` 的 `spinlock_t` 等**与本次原生调度器改动无关**的预存编译错误，需单独修复。
- Phase 1 为验证调度器骨架，尚未启用 Shell/UI/WiFi。Shell/UI 在 Phase 2 启用，tickless idle 与 FreeRTOS API 移除在 Phase 2A 完成。

#### 中文伪代码拆解

```
函数 native_switch_to(目标任务) {
    // 保存调度器上下文
    if (setjmp(g_sched_ctx) == 0) {
        if (目标任务.上下文已建立) {
            // 普通恢复：longjmp 到任务上次 yield 的位置
            longjmp(目标任务.native_ctx, 1)
        } else {
            // 首次运行：切到任务栈并进入包装函数
            xeros_task_start(目标任务)
        }
    }
    // 从 longjmp 返回：任务至少运行过一次，上下文已可用
    目标任务.上下文已建立 = true
}
```

核心思想：调度器与任务互相 `setjmp`/`longjmp`，像两个“书签”来回跳转；新任务第一次没有“书签”，所以用汇编桩直接“翻开新书”。

---

## Phase 2 实施记录

### 2026-06-26：启用 Shell/UI 进入原生调度器

#### 根因

在 Phase 1 为了最小化崩溃面，`src/main.cpp` 在 `XEROS_NATIVE_SCHED` 下用 `#ifndef` 显式屏蔽了 `kern_shell_init()`、`ui_task_main` 和 `main_loop_task`，只运行最小 `native_bringup_init()`。因此**不是 Shell/UI 在原生调度器下跑不起来，而是根本没有被启动**。

*📄 Source: [main.cpp](../src/main.cpp#L394-L396)*

#### 修复步骤

1. **移除屏蔽，启用完整任务集**

   在 `XEROS_NATIVE_SCHED` 下同样调用 `kern_shell_init()`，创建 `ui`、`main-loop` 任务；仅保留 `wifi-mgr` 在原生下禁用（其依赖 FreeRTOS 内部事件组，Phase 2 后续迁移）。

   *📄 Source: [main.cpp](../src/main.cpp#L379-L396)*

2. **关闭原生调度器高频切换日志**

   `kern_port_esp32_native.c` 中 `native_switch_to` / `native_task_yield` 的 `debug_printf` 在完整任务集下每秒输出数千行，淹没 Shell 与 UI 日志。引入 `NATIVE_SCHED_DEBUG` 开关，默认关闭。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L34-L40)*

4. **WiFi 请求调用保护**

   `main_loop_task` 中 `wifi_mgr_process_requests()` 在原生下用 `#ifndef XEROS_NATIVE_SCHED` 包裹，避免 WiFi 未初始化时产生异常。

   *📄 Source: [main.cpp](../src/main.cpp#L182-L184)*

#### 中文伪代码拆解

```
函数 deferred_kernel_init() {
    // 内核子系统初始化（VFS、devfs、procfs、sysfs、gpiofs、devices）
    初始化内核子系统()

    // 启动 Shell：创建 shell 任务，绑定 /dev/ttyS0
    kern_shell_init()

    // 启动 UI 任务：使用推荐栈大小
    创建任务("ui", ui_task_main, UI栈大小)

    // 启动主循环任务：轮询串口、串口监视器
    创建任务("main-loop", main_loop_task, 4096)

    // WiFi 任务仍依赖 FreeRTOS，原生下暂不创建
    if (不是原生调度器) {
        创建任务("wifi-mgr", wifi_mgr_task_main, WiFi栈大小)
    }
}
```

核心思想：Phase 1 验证的是调度器骨架，Phase 2 要把完整应用任务重新接回调度器；失败原因不是调度器支撑不了，而是启动代码把它们注释掉了。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| Shell `ps` 命令 | 串口发送 `ps` | **显示 xidle1/shell/ui/main-loop 四个任务** |
| Shell `ls` 命令 | 串口发送 `ls` | **显示 /dev /proc /sys** |
| Shell `help` 命令 | 串口发送 `help` | **列出所有内置命令** |
| UI 任务运行 | 串口日志 | **ui frame 1-5 begin/flush done 循环输出** |
| 120 秒稳定性测试 | 串口抓取 120s | **0 次复位，0 次 Guru Meditation** |

#### 仍遗留的问题

- **WiFi 管理器**：`wifi_mgr_task_main` 内部仍使用 FreeRTOS 事件组/队列，Phase 2 后续需要原生化或确认可在 FreeRTOS 任务与 Xeros 任务共存模式下工作。
- **UI 实际屏幕显示**：从日志看 UI 任务持续刷新帧，但屏幕是否最终正确渲染需用户现场确认（背光、菜单内容）。若屏幕无显示，需进一步排查 `hal_display_flush` 与 LovyanGFX 在原生调度器下的时序。
- **原生单元测试基线错误**：`pio test -e native` 中 `dev_ttyS0.cpp` 的 `g_ttyS0_mux` 和 `test_kernel_sync.cpp` 的 `spinlock_t` 等预存编译错误仍存在，与本次改动无关。

### 2026-06-26：tickless idle 与移除 FreeRTOS 调度 API

#### 目标

- 当系统空闲时，按下一个唤醒事件重编程 GPTimer，避免固定 1ms tick  busy-loop。
- 在 `XEROS_NATIVE_SCHED` 路径下，移除 Xeros 任务对 FreeRTOS 调度 API（`vTaskDelay`、`vTaskPrioritySet` 等）的显式调用。

#### 关键改动

1. **tick_timer 支持动态重编程**

   在 `tick_timer.c` 中保存初始化周期 `g_period_us`，新增 `tick_timer_set_next_alarm()` 与 `tick_timer_restore_periodic()`，分别用于设置单次唤醒 alarm 和恢复周期性 tick。

   *📄 Source: [tick_timer.c](../src/kernel/esp32/tick_timer.c#L30-L199)*
   *📄 Source: [tick_timer.h](../src/kernel/esp32/tick_timer.h#L65-L80)*

2. **native_idle 实现 tickless 路径**

   `kern_port_esp32_native.c` 的 `native_idle()` 在 `g_current_task == g_idle_task` 时：
   - 扫描全局任务链表，找到最近的 `SLEEPING` 任务唤醒时间；
   - 若空闲时长大于等于 `TICKLESS_MIN_IDLE_MS`（2ms），调用 `tick_timer_set_next_alarm(idle_us)`；
   - 低功耗忙等 `tick_timer_pending()`，唤醒后恢复周期性 tick；
   - 无法进入 tickless 时回退到 `ets_delay_us(1000)`，不再调用 `vTaskDelay`。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L145-L191)*

3. **移除 `main.cpp` 中的 FreeRTOS 显式调用**

   - 删除 `vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 1)`；
   - 将 `app_main` 调度循环中的 `vTaskDelay(pdMS_TO_TICKS(1))` 替换为 `kern_port_idle()`，让 Xeros 原生 idle 路径接管。

   *📄 Source: [main.cpp](../src/main.cpp#L275-L284)*

4. **hal_system 移除 `vTaskDelay` fallback**

   `hal_delay_ms()` 在 `g_current_task == NULL` 时，原生调度器下使用 `ets_delay_us()` 而非 `vTaskDelay()`。

   *📄 Source: [hal_system.cpp](../src/hal/hal_system.cpp#L54-L86)*

5. **完全移除原生后端的 FreeRTOS 头文件依赖**

   `kern_port_esp32_native.c` 不再包含 `<freertos/FreeRTOS.h>` 和 `<freertos/task.h>`，错误路径与 fallback 均改用 `ets_delay_us()`。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L28-L32)*

#### 踩坑：`waiti 0` 在 user mode 下触发异常

第一版 tickless 实现尝试在 idle 中执行 Xtensa `waiti 0` 指令等待中断，结果设备启动超时。根因：`waiti` 是特权指令，当前调度器运行在 FreeRTOS `app_main` 任务的 user mode（PS.UM=1）下，直接执行会触发异常。

修复：用低功耗忙等（`nop` 循环轮询 `tick_timer_pending()`）替代 `waiti 0`。这不是最终低功耗方案，但 Phase 2A 以稳定性优先，真正的睡眠留给后续阶段评估。

#### 中文伪代码拆解

```
函数 native_idle() {
    if (当前任务 == idle 任务) {
        下一唤醒时间 = 扫描所有 SLEEPING 任务的最小 wake_time
        if (存在唤醒时间 且 空闲时长 >= 2ms) {
            限制最大睡眠 100ms
            重编程 GPTimer 下一次 alarm
            while (没有 pending tick) {
                nop   // 低功耗忙等，替代 waiti 0
            }
            恢复周期性 tick
            return
        }
    }
    // 无法进入 tickless：回退到 ROM 延时 1ms
    ets_delay_us(1000)
}
```

核心思想：把固定节拍变成“按需唤醒”——有任务睡觉时按闹钟醒来，没任务睡觉时也不依赖 FreeRTOS 调度器。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 默认环境编译 | `pio run -e m5stick-c` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| 启动 | 串口抓取 | **正常进入 Xeros Shell，UI 任务开始渲染** |
| Shell `ps` | 串口发送 `ps` | **显示 xidle1/shell/ui/main-loop 四个任务** |
| Shell `help` / `ls` | 串口发送 | **命令响应正常** |
| 120 秒稳定性测试 | 串口抓取 120s | **0 次复位，0 次 Guru Meditation，UI 帧持续刷新** |
| 后端 FreeRTOS API 残留 | `grep vTaskDelay src/kernel/kern_port_esp32_native.c` | **仅剩注释提及，无实际调用** |

#### 仍遗留的问题

- **UI 实际屏幕显示**：从日志看 UI 任务持续刷新帧，但屏幕是否最终正确渲染需用户现场确认。
- **原生单元测试基线错误**：`pio test -e native` 中 `dev_ttyS0.cpp` 的 `g_ttyS0_mux` 和 `test_kernel_sync.cpp` 的 `spinlock_t` 等预存编译错误仍存在，与本次改动无关。

### 2026-06-26：在原生调度器下启用 WiFi

#### 目标

- 让 WiFi 管理器作为普通 Xeros 任务在原生调度器下运行，恢复完整应用功能。
- 确认 ESP-IDF WiFi 驱动内部的 FreeRTOS 任务与 Xeros 原生调度器可以共存。

#### 关键改动

1. **移除 `wifi-mgr` 的禁用包裹**

   `deferred_kernel_init()` 中原先用 `#ifndef XEROS_NATIVE_SCHED` 跳过 `wifi-mgr` 任务创建。Phase 2B 中移除该包裹，原生调度器下同样创建 `wifi-mgr` 任务。

   *📄 Source: [main.cpp](../src/main.cpp#L392-L394)*

2. **恢复 WiFi 请求处理**

   `main_loop_task()` 中原先用 `#ifndef XEROS_NATIVE_SCHED` 跳过 `wifi_mgr_process_requests()`。移除包裹后，主循环任务继续将 UI 发来的 WiFi 开关请求转发给 WiFi 管理器。

   *📄 Source: [main.cpp](../src/main.cpp#L179-L184)*

3. **注释更新**

   将 `main.cpp` 中“WiFi 管理器仍依赖 FreeRTOS 内部事件组，Phase 2 再迁移”的注释更新为说明：WiFi 驱动内部仍使用 FreeRTOS，但 Xeros 的 `wifi-mgr` 任务本身只调用 ESP-IDF WiFi API，不直接调用 FreeRTOS 调度 API。

#### 中文伪代码拆解

```
函数 deferred_kernel_init() {
    // ... 内核子系统、Shell、UI、主循环任务 ...

    // Phase 2B：原生调度器下也创建 WiFi 管理器任务
    创建任务("wifi-mgr", wifi_mgr_task_main, WiFi栈大小)
}

函数 main_loop_task() {
    for (;;) {
        串口轮询()
        串口监视器更新()
        wifi_mgr_process_requests()   // Phase 2B：恢复 WiFi 请求处理
        周期性栈水印采样()
        kern_sleep_ms(10)
    }
}
```

核心思想：WiFi 管理器本来就是 Xeros 任务（使用 `kern_poll_loop` + `kern_sleep_ms`），之前被屏蔽只是因为担心 ESP-IDF WiFi 驱动的 FreeRTOS 内部任务会与原生调度器冲突；Phase 2B 验证两者可以共存。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 默认环境编译 | `pio run -e m5stick-c` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| 启动 | 串口抓取 | **正常进入 Xeros Shell，UI 任务开始渲染** |
| Shell `ps` | 串口发送 `ps` | **显示 xidle1/shell/ui/main-loop/wifi-mgr 五个任务** |
| 120 秒稳定性测试 | 串口抓取 120s | **0 次复位，0 次 Guru Meditation，UI 帧持续刷新，wifi-mgr 保持 READY** |
| WiFi 驱动初始化 | 启动日志 | **WiFi 驱动正常初始化并进入 STA 模式** |

#### 仍遗留的问题

- **UI 实际屏幕显示与 WiFi 菜单操作**：从日志看任务运行正常，但 UI 上开关 WiFi 菜单、扫描/连接等实际操作需用户现场确认。
- **原生单元测试基线错误**：`pio test -e native` 中 `dev_ttyS0.cpp` 的 `g_ttyS0_mux` 和 `test_kernel_sync.cpp` 的 `spinlock_t` 等预存编译错误仍存在，与本次改动无关。

### 2026-06-26：原生 SMP 双核调度

#### 目标

- 在 ESP32 双核上运行 Xeros 调度器，每个核心独立调度绑定到该核心的任务。
- `CONFIG_SMP_ENABLED` 下不再把 SMP 代码当作单核兼容模式处理。

#### 关键改动

1. **启用 SMP 编译开关**

   在 `platformio.ini` 的 `[env:m5stick-c-native]` 中增加 `-D CONFIG_SMP_ENABLED`。

   *📄 Source: [platformio.ini](../platformio.ini#L64)*

2. **CPU ID 原生化**

   `kern_smp.c` 的 `kern_cpu_id()` 使用 `esp_cpu_get_core_id()` 读取 PRID 特殊寄存器，替代 `xPortGetCoreID()`。

   *📄 Source: [kern_smp.c](../src/kernel/kern_smp.c#L20-L35)*

3. **per-CPU 调度器上下文**

   `kern_sched.c` 中 `g_sched_ctx` 从全局标量改为数组 `kern_ctx_native_t g_sched_ctx[KERN_MAX_CPUS]`；`kern_port_esp32_native.c` 通过 `sched_ctx_current()` 宏索引当前 CPU。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L113)*
   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L35-L40)*

4. **APP_CPU 调度循环启动**

   `kern_sched_init()` 调用 `kern_smp_start_core(1, kern_smp_sched_loop)` 创建 FreeRTOS 启动桩；`app_main()` 在 Core 0 直接进入 `kern_smp_sched_loop(NULL)`，Core 1 通过启动桩进入同一函数。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L249-L252)*
   *📄 Source: [main.cpp](../src/main.cpp#L270-L275)*

5. **任务 CPU 亲和性**

   - idle 任务创建时绑定对应 CPU（`cpu_id = cpu`）。
   - `kern_smp_migrate_assign()` 将 `KERN_CPU_ANY` 任务按负载分配到当前核心。
   - `sched_rr_pick_next()` 与 `sched_fifo_pick_next()` 只选择 `cpu_id == KERN_CPU_ANY` 或 `cpu_id == 当前CPU` 的任务；无匹配时返回本核心 idle。

   *📄 Source: [kern_sched_rr.c](../src/kernel/kern_sched_rr.c#L136-L145)*
   *📄 Source: [kern_sched_fifo.c](../src/kernel/kern_sched_fifo.c#L119-L125)*

6. **暴露 `kern_smp_sched_loop` 声明**

   将该函数声明加入 `kern_smp.h`，使 `main.cpp` 在 SMP 模式下可直接调用。

   *📄 Source: [kern_smp.h](../src/kernel/kern_smp.h#L59-L61)*

#### 踩坑：`g_task_list_tail` 初始化错误导致 `xidle0` 从任务链表断开

SMP 下创建两个 idle 任务时采用头插法：

```
idle->next = g_task_list;
g_task_list = idle;
```

创建完成后原代码直接写 `g_task_list_tail = g_task_list`，结果 tail 指向链表头（`xidle1`），而非链表尾（`xidle0`）。后续追加任务时把新任务接在 `xidle1` 后面，覆盖了 `xidle1->next`，导致 `xidle0` 丢失。

表现：`ps` 命令只显示 `xidle1`，不显示 `xidle0`。

修复：在 `kern_sched_init()` 中遍历链表找到真正的尾节点再赋值给 `g_task_list_tail` 与 `sched_class_rr.task_list_tail`。

*📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L239-L248)*

#### 中文伪代码拆解

```
函数 kern_sched_init() {
    初始化 SMP 子系统()
    初始化原生端口()
    注册 RR/FIFO 调度类()

    for (cpu = 0; cpu < 2; cpu++) {
        idle = 创建 idle 任务("xidle{cpu}")
        idle.cpu_id = cpu
        头插法加入全局任务链表
        g_per_cpu[cpu].idle_task = idle
    }

    // 关键：tail 必须指向链表最后一个元素
    tail = 遍历到 g_task_list 末尾
    g_task_list_tail = tail
    sched_class_rr.task_list_tail = tail

    g_per_cpu[0].current_task = xidle0
    g_per_cpu[1].current_task = xidle1

    // Core 1 通过 FreeRTOS 启动桩进入调度循环
    kern_smp_start_core(1, kern_smp_sched_loop)
}

函数 kern_smp_sched_loop(arg) {
    cpu = 当前 CPU ID
    g_per_cpu[cpu].current_task = g_per_cpu[cpu].idle_task
    标记本核心就绪
    while (true) {
        if (有 pending tick) g_need_resched = true
        kern_sched_tick()
        kern_port_idle()
    }
}

函数 app_main() {
    // ... 硬件初始化、内核延迟初始化 ...
    for (;;) {
        if (SMP 模式) {
            // Core 0 直接变成 Xeros 调度器，不再返回
            kern_smp_sched_loop(NULL)
        } else {
            单核调度循环()
        }
    }
}
```

核心思想：每个 CPU 拥有独立的 `current_task`、`idle_task` 和调度器上下文；全局任务链表按 CPU 亲和性共享；Core 0 由 `app_main` 亲自进入调度循环，Core 1 借 FreeRTOS 任务做启动桩。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 默认环境编译 | `pio run -e m5stick-c` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| 启动日志 | 串口抓取 | **SMP: 2 CPUs initialized / core 1 scheduler started / core 0 scheduler entering loop** |
| Shell `ps` | 串口发送 `ps` | **显示 xidle0/xidle1/shell/ui/main-loop/wifi-mgr 六个任务** |
| 双核任务状态 | `ps` 输出 | **shell 与 ui 同时 RUNNING，分布在两个核** |
| 120 秒稳定性测试 | 串口抓取 120s | **0 次复位，0 次 Guru Meditation，UI 帧持续刷新** |

#### 仍遗留的问题

- **IPI（处理器间中断）**：当前任务绑定到创建时分配的 CPU，不会跨核迁移。若一个核唤醒了绑定在另一核上的任务，目标核需等到下一次 tick 才能调度；Phase 2C 先以“不迁移”降低复杂度，IPI 优化留给后续阶段。
- **IPC/SMP 安全**：`g_task_list_lock` 已在 `CONFIG_SMP_ENABLED` 下启用，但 IPC 等待队列、同步原语仍需系统性 review 加锁。
- **UI 实际屏幕显示与 WiFi 菜单操作**：需用户现场确认。
- **原生单元测试基线错误**：`pio test -e native` 中 `dev_ttyS0.cpp` 的 `g_ttyS0_mux` 和 `test_kernel_sync.cpp` 的 `spinlock_t` 等预存编译错误仍存在，与本次改动无关。

### 2026-06-27：修复 UI 长时间卡死与串口监视器滑块自切换

#### 问题现象

1. **UI 长时间运行后卡死**：系统运行一段时间后，屏幕 UI 不再刷新，但 Shell 和其他任务（`main-loop`、`wifi-mgr`）仍正常响应。
2. **串口监视器滑块自切换**：第一次进入串口监视器 App 后，未进行任何按键操作，顶部滑块（START/STOP ↔ SER）却会自动切换。

#### 根因分析

**根因 1：SMP 下全局 GPTimer alarm 被两个核互相覆盖**

`native_idle()` 为支持 tickless idle，会按下一个 SLEEPING 任务的唤醒时间重编程 GPTimer。但 GPTimer 是全局硬件资源，Core 0 和 Core 1 都会调用 `tick_timer_set_next_alarm()`。如果 Core 0 要 5ms 后唤醒 UI 任务，而 Core 1 紧接着设置了一个 100ms 的 alarm，Core 0 的短 alarm 会被覆盖，UI 任务的 `kern_sleep_ms(5)` 被延迟到 100ms 后才唤醒。频繁发生后，UI 帧率骤降，表现为“卡死”。

*📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L165-L198)*

**根因 2：SMP 下同一 READY 任务可能被两个核同时选中**

`sched_rr_pick_next()` 与 `sched_fifo_pick_next()` 在自旋锁内扫描全局任务链表，但**只检查状态，不在锁内把选中任务标记为 RUNNING**。两个 CPU 可能同时看到同一个 READY 任务并都返回它，随后两个核都调用 `kern_port_switch_to(task)`，导致同一个任务在两个核上并行运行，上下文和栈状态混乱。极端情况下会触发崩溃或任务状态机损坏。

*📄 Source: [kern_sched_rr.c](../src/kernel/kern_sched_rr.c#L136-L157)（修复前）*

**根因 3：被切换出去的任务状态未正确回退到 READY**

时间片用完后，`sched_rr_tick()` 只设置 `g_need_resched = true`，没有将当前运行任务状态改回 READY。`kern_sched_tick()` 在切换前也没有重置 prev 状态。这会导致被抢占的任务长期处于 RUNNING，后续难以再次被调度。

*📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L415-L424)（修复前）*

**根因 4：`hal_input_reset_events()` 未同步 `prev_raw`**

进入串口监视器 App 时调用 `ui_service_user_item_init()` → `hal_input_reset_events()`，该函数清除了双击状态机字段，但没有把 `prev_raw` 同步为当前 GPIO 电平。如果用户进入 App 前按键处于按下状态，`prev_raw` 仍为 `true`；App 第一次调用 `hal_input_get_event()` 时读到按键已释放，产生 `wasReleased=true`，由于双击检测已启用，状态机设置 `pending_short_press=true`，300ms 窗口超时后返回 `HAL_EVENT_SHORT_PRESS`，导致 `sm_selected` 自动切换。

*📄 Source: [hal_input.cpp](../src/hal/hal_input.cpp#L231-L238)（修复前）*

#### 修复步骤

1. **限制 tickless 重编程仅 Core 0 执行**

   在 `native_idle()` 开头判断 `kern_cpu_id() != 0` 时直接 `ets_delay_us(1000)` 返回，避免 Core 1 覆盖 Core 0 的 alarm。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L168-L172)*

2. **在调度类 pick_next 中把选中任务原子标记为 RUNNING**

   - `sched_rr_pick_next()` 返回前在锁内将任务 `state = KERN_TASK_RUNNING`；返回本核心 idle 时同样标记。
   - `sched_fifo_pick_next()` 返回前在锁内将任务 `state = KERN_TASK_RUNNING`。

   *📄 Source: [kern_sched_rr.c](../src/kernel/kern_sched_rr.c#L140-L157)*
   *📄 Source: [kern_sched_fifo.c](../src/kernel/kern_sched_fifo.c#L119-L126)*

3. **`kern_sched_tick()` 切换任务前把 prev 状态改回 READY**

   在 ESP32 fallback 的 `kern_sched_tick()` 中，切换前检查 `prev->state == KERN_TASK_RUNNING` 则改为 `KERN_TASK_READY`，避免被切换出去的任务长期占用 RUNNING 状态。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L415-L428)*

4. **避免自己切换到自己**

   `kern_sched_tick()` 中如果 `pick_next_ready()` 返回的任务就是当前任务，直接跳过 `kern_port_switch_to()`。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L417-L418)*

5. **修复 `hal_input_reset_events()` 同步 `prev_raw`**

   在 `hal_input.cpp` 的 `hal_input_reset_events()` 中，重置双击状态机后，立即把 `g_btn_a.prev_raw` 和 `g_btn_b.prev_raw` 同步为当前 GPIO 电平，防止 reset 后产生假边沿。

   *📄 Source: [hal_input.cpp](../src/hal/hal_input.cpp#L231-L241)*

#### 中文伪代码拆解

```
函数 native_idle() {
    if (当前 CPU 不是 Core 0) {
        // Core 1 不控制全局 timer，避免互相覆盖 alarm
        ets_delay_us(1000)
        return
    }
    // Core 0 才执行 tickless 重编程 ...
}

函数 sched_rr_pick_next() {
    加锁()
    唤醒到期 sleep 任务()
    按 Round-Robin 扫描 READY 任务 {
        if (亲和性匹配) {
            task.state = RUNNING   // 在锁内标记，防止其他核同时选中
            解锁()
            return task
        }
    }
    idle = 本核 idle 任务
    idle.state = RUNNING
    解锁()
    return idle
}

函数 kern_sched_tick() {
    g_sched_ticks++
    调用调度类 tick 回调()
    if (需要重调度) {
        next = pick_next_ready()
        if (next != NULL 且 next != 当前任务) {
            if (当前任务.state == RUNNING) {
                当前任务.state = READY
            }
            g_current_task = next
            kern_port_switch_to(next)
        }
    }
}

函数 hal_input_reset_events() {
    初始化双击状态机(A)
    初始化双击状态机(B)
    A.prev_raw = 读取 GPIO A 当前电平
    B.prev_raw = 读取 GPIO B 当前电平
}
```

核心思想：SMP 下调度必须保证“选中即占用”，用任务状态本身作为跨核的选择锁；全局硬件资源（GPTimer）只能由一个核控制；输入状态机 reset 时必须把物理边沿也同步，否则 reset 后第一次读取会误判。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| 原生环境编译 | `pio run -e m5stick-c-native` | 通过 |
| 默认环境编译 | `pio run -e m5stick-c` | 通过 |
| 烧录 | `pio run -e m5stick-c-native -t upload` | 通过 |
| 启动日志 | 串口抓取 | **正常进入 Shell，UI 任务渲染** |
| Shell `ps` | 串口发送 `ps` | **显示六个任务，状态正常** |
| 120 秒稳定性测试 | 串口抓取 120s | **0 次复位，0 次 Guru Meditation** |
| 300 秒稳定性测试 | 串口抓取 300s | **0 次复位，UI 未报 stuck** |
| 串口监视器滑块自切换 | 实机操作 | **需用户现场确认是否修复** |
| UI 长时间卡死 | 实机操作 | **需用户现场确认是否修复** |

#### 仍遗留的问题

- **UI 实际屏幕显示与串口监视器操作**：调度器层面的竞态已修复，但实际屏幕是否仍有卡顿、串口监视器滑块是否还会自切换，需要用户现场确认。
- **IPI（处理器间中断）**：仍未实现，任务严格绑定创建时分配的 CPU。
- **IPC/SMP 安全**：已完成系统性 review 并修复，详见下文[IPC/SMP 安全审查与修复](#ipcsmp-安全审查与修复)。
- **原生单元测试基线错误**：`pio test -e native` 仍有预存失败（`ContextSwitch.NativeCtxSizeMatchesXtensaWindowed` 与测试结束 SIGHUP），与本次改动无关。

### 2026-06-27：调试与诊断扩展（Phase 2.4）

#### 目标

- 扩展 `tools/xeros_debug.py`，使其能主动抓取 SMP 核状态、统计 tickless 平均 tick 间隔，并自动识别 Guru Meditation 与复位原因。
- 为后续长稳测试提供可脚本化的观测入口，减少人工盯串口的成本。

#### 关键改动

1. **新增 `--smp` 模式**

   调试工具通过反复发送 Shell `ps` 命令并解析输出，汇总每个 CPU 上当前 RUNNING 的任务以及采样期间出现过的任务。解析器匹配形如 `  0 RUNNING       xidle0   cpu=0` 的行。

   *📄 Source: [xeros_debug.py](../tools/xeros_debug.py#L231-L243)*

   使用示例：

   ```bash
   python3 tools/xeros_debug.py /dev/ttyUSB0 --smp --duration 3 --interval 0.5
   ```

2. **新增 `--tickless` 统计**

   在指定时长内监控 `[native]` 调试日志（需打开 `NATIVE_SCHED_DEBUG`），根据时间戳计算 tick 间隔的最小、最大与平均值。如果未检测到相关日志，会提示打开调试开关。

   *📄 Source: [xeros_debug.py](../tools/xeros_debug.py#L297-L320)*

   使用示例：

   ```bash
   python3 tools/xeros_debug.py /dev/ttyUSB0 --tickless --duration 10
   ```

3. **自动检测 Guru Meditation 与复位原因**

   在 `read_output()`、`read_until()`、`monitor()` 以及 `--tickless` 采样路径中统一调用 `detect_anomalies()`。当日志中出现 `Guru Meditation Error:` 或 `rst:0x... (...)` 时立即在终端打印告警，并累计计数。

   *📄 Source: [xeros_debug.py](../tools/xeros_debug.py#L231-L243)*

4. **实时日志与异常告警联动**

   `monitor()` 监控线程在把日志写入文件/终端的同时，也会调用 `detect_anomalies()`。这样长稳测试时无需持续盯屏，异常一旦出现就会直接打印到终端。

   *📄 Source: [xeros_debug.py](../tools/xeros_debug.py#L159-L174)*

#### 中文伪代码拆解

```
函数 detect_anomalies(日志行) {
    if (日志行包含 "Guru Meditation Error") {
        guru 计数++
        打印告警(日志行摘要)
    }
    if (日志行匹配 "rst:0x.. (原因)") {
        复位计数++
        打印告警(复位原因)
    }
}

函数 parse_ps_output(ps 输出) {
    结果 = { CPU0: [], CPU1: [] }
    遍历每一行 {
        if (行匹配 "PID STATE NAME cpu=ID") {
            if (STATE == RUNNING) {
                结果[ID].append(NAME)
            }
        }
    }
    return 结果
}

函数 sample_smp(采样时长, 采样间隔) {
    while (未到结束时间) {
        发送 "ps"
        解析输出并记录
        等待间隔
    }
    汇总每个 CPU 出现过的任务与当前 RUNNING 任务
}

函数 tickless_stats(采样时长) {
    while (未到结束时间) {
        读取日志行
        if (行包含 "[native]") {
            记录时间戳
        }
        detect_anomalies(行)
    }
    if (记录数 < 2) {
        提示打开 NATIVE_SCHED_DEBUG
    } else {
        计算 avg/min/max tick 间隔
    }
}
```

核心思想：把“人眼盯串口”变成“工具主动采样 + 异常即时告警”。`ps` 命令成为 SMP 状态的快照接口，`[native]` 调试日志成为 tickless 精度的间接探针。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| Python 语法检查 | `python3 -m py_compile tools/xeros_debug.py` | 通过 |
| `--smp` 解析 | `python3 tools/xeros_debug.py /dev/ttyUSB0 --smp --duration 2` | **正确解析出 CPU0/CPU1 当前 RUNNING 任务** |
| `--tickless` 无日志提示 | `python3 tools/xeros_debug.py /dev/ttyUSB0 --tickless --duration 3` | **提示打开 NATIVE_SCHED_DEBUG** |
| 复位原因检测 | 模拟日志 `rst:0x8 (TG1WDT_SYS_RESET)` | **识别为 TG1WDT_SYS_RESET** |
| Guru Meditation 检测 | 模拟日志 `Guru Meditation Error: Core 0 panic'ed` | **识别并计数** |

#### 仍遗留的问题

- `--tickless` 统计依赖 `NATIVE_SCHED_DEBUG` 输出，关闭时无法直接测量硬件 tick 间隔；后续可在内核中增加一条周期性的 `tick` 诊断日志，或在 `tick_timer.c` 中记录 alarm 触发时间戳供工具读取。
- 当前 SMP 采样通过轮询 `ps` 实现，时间分辨率受串口响应影响；如需更高精度，可在内核中增加一个只读的 procfs 节点暴露 `g_per_cpu[]` 状态。
- **IPI（处理器间中断）**：仍未实现，任务严格绑定创建时分配的 CPU。
- **IPC/SMP 安全**：已完成系统性 review 并修复，详见下文[IPC/SMP 安全审查与修复](#ipcsmp-安全审查与修复)。
- **原生单元测试基线错误**：`pio test -e native` 仍有预存失败（`ContextSwitch.NativeCtxSizeMatchesXtensaWindowed` 与测试结束 SIGHUP），与本次改动无关。

### 2026-06-27：IPC/SMP 安全审查与修复

> **Parent:** [调试记录索引](#调试记录索引) | **Prev:** [调试与诊断扩展（Phase 2.4）](#2026-06-27调试与诊断扩展phase-24)

#### 目标

- 系统性审查 Xeros IPC 原语（二值信号量、计数信号量、PI 互斥锁、消息队列、事件组）与 SMP 相关代码在 `CONFIG_SMP_ENABLED` 下的并发安全。
- 修复发现的竞态，确保双核调度时任务状态、等待队列、IPC 计数器不会被并发破坏。

#### 审查范围

| 模块 | 文件 | 关注点 |
|------|------|--------|
| IPC 原语 | `src/kernel/kern_ipc.c` / `kern_ipc.h` | 信号量 count、等待队列、PI 优先级继承、队列环形缓冲区、事件位图 |
| 同步原语 | `src/kernel/kern_sync.c` / `kern_sync.h` | `xeros_spinlock_t`、`mutex_t` 的 SMP 实现 |
| 调度器 | `src/kernel/kern_sched_rr.c` / `kern_sched_fifo.c` | 任务链表访问、状态转换、CPU 亲和性 |
| 任务生命周期 | `src/kernel/kern_task_lifecycle.c` | spawn/kill/exit 与全局任务链表的交互 |
| SMP 支持 | `src/kernel/kern_smp.c` | CPU 分配、per-CPU 状态初始化 |
| tickless idle | `src/kernel/kern_port_esp32_native.c` | `native_idle_next_wake_ms()` 遍历全局任务链表 |

#### 发现的问题

1. **IPC 原语完全无锁**

   `kern_bin_sem_t`、`kern_sem_t`、`kern_pi_mutex_t`、`kern_queue_t`、`kern_event_t` 结构体中均未包含自旋锁。`kern_bin_sem_give()` 与 `kern_bin_sem_take()` 可能同时修改 `count` 和 `wait_queue`；`kern_queue_send()` 与 `kern_queue_recv()` 可能同时修改 `head/tail/count`。在 SMP 下会导致队列损坏或信号量计数异常。

2. **IPC 超时唤醒后节点残留**

   `ipc_block_task()` 把栈上的 `kern_wait_node_t` 插入 IPC 等待队列后 yield。当前超时就绪检查分散在 `sched_rr_pick_next()` / `sched_fifo_pick_next()` 中直接修改 `task->state = READY`，但**没有**把节点从 IPC 等待队列移除。如果任务随后被 `give()` 再次唤醒，`ipc_wait_dequeue()` 会返回一个已经就绪的任务，并再次设置其状态；更严重的是，如果任务在超时后退出阻塞并返回用户栈，`give()` 可能访问已释放的栈内存。

3. **`native_idle_next_wake_ms()` 无锁遍历全局任务链表**

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L148-L176)*

   tickless idle 计算下一个 SLEEPING 任务唤醒时间时直接遍历 `g_task_list`，而 `kern_spawn()`、`reap_zombies()`、`sched_rr_pick_next()` 等路径可能并发修改链表或任务状态。

4. **`kern_smp_migrate_assign()` 的负载计数非原子**

   *📄 Source: [kern_smp.c](../src/kernel/kern_smp.c#L84-L112)*

   两个 CPU 同时 `kern_spawn()` 时，可能同时读取到 `g_per_cpu[0].task_count <= g_per_cpu[1].task_count`，然后都把任务分配给 CPU0，导致负载失衡。

5. **VFS dentry-tree 仍无锁**

   `src/kernel/kern_vfs.c` 的目录树操作没有全局锁保护。当前上层调用基本发生在单一任务上下文，SMP 下如果多个任务并发创建/删除文件/目录会出现竞态。本次未改动，作为遗留风险记录。

#### 修复方案

1. **给每个 IPC 结构体增加 `xeros_spinlock_t lock`**

   在 `kern_ipc.h` 中为所有 IPC 原语增加自旋锁字段，并通过 `kern_ipc.h` 包含 `kern_sync.h`。

   *📄 Source: [kern_ipc.h](../src/kernel/kern_ipc.h#L44-L144)*

2. **所有 IPC 操作函数内部持锁**

   `kern_bin_sem_give/take`、`kern_sem_init/give/take/get_count`、`kern_pi_mutex_init/lock/unlock`、`kern_queue_init/send/recv/count`、`kern_event_init/set/clear/get/wait` 均在访问结构体字段前后加锁/解锁。

   *📄 Source: [kern_ipc.c](../src/kernel/kern_ipc.c#L113-L560)*

3. **`ipc_block_task()` 自清理残留节点**

   被唤醒返回后，在对应 IPC 自旋锁保护下再次扫描等待队列，如果自己的节点仍残留则移除。这样无论被 `give()` 提前唤醒还是被调度器按 `wake_time` 超时唤醒，都不会留下失效节点。

   *📄 Source: [kern_ipc.c](../src/kernel/kern_ipc.c#L91-L124)*

4. **`native_idle_next_wake_ms()` 持 `g_task_list_lock` 读取**

   在遍历 `g_task_list` 前后获取/释放全局任务链表自旋锁，与 `kern_spawn()`、`reap_zombies()` 等保持一致。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L148-L176)*

5. **`kern_smp_migrate_assign()` 用本地自旋锁保护计数器**

   在读取 `g_per_cpu[0/1].task_count` 和递增之间持锁，避免两个核心同时分配到同一 CPU。

   *📄 Source: [kern_smp.c](../src/kernel/kern_smp.c#L84-L107)*

#### 中文伪代码拆解

```
函数 kern_bin_sem_take(信号量, 超时) {
    上锁(信号量.lock)

    if (信号量.count > 0) {
        信号量.count = 0
        解锁(信号量.lock)
        return 成功
    }

    if (超时 == 0) {
        解锁(信号量.lock)
        return 超时
    }

    解锁(信号量.lock)
    ipc_block_task(信号量.wait_queue, 信号量.lock, 超时)

    上锁(信号量.lock)
    if (信号量.count > 0) {
        信号量.count = 0
        解锁(信号量.lock)
        return 成功
    }
    解锁(信号量.lock)
    return 超时
}

函数 ipc_block_task(队列, 锁, 超时) {
    当前任务 = kern_task_current()
    节点 = { task=当前任务, timeout=当前tick+超时 }

    上锁(锁)
    当前任务.state = SLEEPING
    ipc_wait_enqueue(队列, 节点)   // 调用者已持锁
    解锁(锁)
    kern_yield()                    // 切出，等待被唤醒

    // 回到此处后：如果 give 已把节点移出，下面循环找不到；
    // 如果是调度器按 wake_time 唤醒，节点可能还在队列中，需要清理。
    上锁(锁)
    遍历队列 {
        if (当前节点 == 节点) {
            从队列移除
            break
        }
    }
    解锁(锁)
}

函数 native_idle_next_wake_ms() {
    当前tick = g_sched_ticks
    下一个 = 0

    SMP下上锁(g_task_list_lock)
    遍历 g_task_list {
        if (任务.state == SLEEPING 且 任务.wake_time > 当前tick) {
            if (下一个 == 0 或 任务.wake_time < 下一个) {
                下一个 = 任务.wake_time
            }
        }
    }
    SMP下解锁(g_task_list_lock)
    return 下一个
}
```

核心思想：**把 IPC 原语的内部状态（count、owner、queue、bits）用自旋锁包围起来**，同时保证阻塞任务 yield 前后等待队列的一致性；**把全局任务链表的读取也纳入已有锁保护**，避免 tickless idle 看到半成品的链表结构。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| native 编译 | `pio run -e native` | **通过** |
| m5stick-c-native 编译 | `pio run -e m5stick-c-native` | **通过** |
| native 单元测试（含修复后的 sync 测试） | `pio test -e native` | **编译通过；仍有 2 个预存失败/异常，与本次改动无关** |

> 备注：native 单元测试原先因 `test_kernel_sync.cpp` 使用旧名称 `spinlock_t` 无法编译，本次已同步改为 `xeros_spinlock_t`。测试包中剩余失败为 `ContextSwitch.NativeCtxSizeMatchesXtensaWindowed`（macOS `jmp_buf` 大小与 Xtensa 预期不符）以及测试结束时的 `SIGHUP`，均非 IPC/SMP 改动引入。

#### 仍遗留的风险

- **VFS dentry-tree 并发**：`kern_vfs.c` 目录树未加全局锁，SMP 下多任务并发访问可能损坏树结构。当前未触发，因为主要文件系统操作集中在单任务。
- **自旋锁忙等开销**：`xeros_spinlock_lock()` 使用 `__sync_lock_test_and_set` + `nop` 自旋，临界区较长时可能浪费 CPU。后续可考虑在持有自旋锁时禁止调度或改用互斥锁 + 阻塞等待。

### 2026-06-27：实现原生 SMP IPI（处理器间中断）

#### 目标

- 解决 IPC 唤醒、事件设置、PI 互斥锁释放、任务创建时，被唤醒任务绑定在另一核心，而目标核心处于 idle/tickless 无法及时调度的问题。
- 使用 ESP-IDF 提供的 `esp_ipc_isr_call()` 向对侧核心发送高优先级中断，使其退出 idle 并重新调度。

#### 关键改动

1. **新增 IPI 汇编处理函数**

   在 `src/kernel/esp32/smp_ipi.S` 中实现 `xeros_ipi_reschedule_handler`。该函数被 `esp_ipc_isr_call()` 以 call0 ABI 在目标核心的高优先级中断上下文中调用，仅做一件事：把目标核心 `g_per_cpu[cpu].need_resched` 写为 `true`。

   *📄 Source: [smp_ipi.S](../src/kernel/esp32/smp_ipi.S#L1-L40)*

2. **SMP 层暴露 IPI 接口**

   `kern_smp.h` 新增 `kern_smp_ipi_reschedule(uint8_t cpu_id)`；`kern_smp.c` 中实现：
   - 检查 `cpu_id` 是否有效且不是当前核心；
   - 检查目标核心是否已进入调度循环（`g_cpu_ready` 对应位为 1），避免在核心未就绪时发送 IPI 导致忙等；
   - 在 ESP32 双核上通过 `esp_ipc_isr_call()` 触发对侧核心的 IPI。

   *📄 Source: [kern_smp.h](../src/kernel/kern_smp.h#L64-L72)*
   *📄 Source: [kern_smp.c](../src/kernel/kern_smp.c#L122-L147)*

3. **在 IPC 唤醒路径触发 IPI**

   `kern_ipc.c` 中，所有将等待任务状态改为 `READY` 的路径都追加 IPI：
   - `ipc_wake_one()`（二值信号量、计数信号量、队列 send/recv 唤醒）
   - `kern_pi_mutex_unlock()`（PI 互斥锁释放时直接继承给下一个等待者）
   - `kern_event_set()`（事件组唤醒所有等待者）

   每次唤醒后调用 `kern_smp_ipi_reschedule(task->cpu_id)`，若目标核心不是当前核心则发送 IPI。

   *📄 Source: [kern_ipc.c](../src/kernel/kern_ipc.c#L125-L134)*
   *📄 Source: [kern_ipc.c](../src/kernel/kern_ipc.c#L366-L377)*
   *📄 Source: [kern_ipc.c](../src/kernel/kern_ipc.c#L489-L503)*

4. **任务创建时通知目标核心**

   `kern_task_lifecycle.c` 的 `kern_spawn()`（`XEROS_NATIVE_SCHED` 分支）在将新任务加入调度类后，若其 `cpu_id` 指向另一核心，发送 IPI 使其尽快被调度。

   *📄 Source: [kern_task_lifecycle.c](../src/kernel/kern_task_lifecycle.c#L202-L212)*

5. **idle 循环响应 IPI**

   `kern_port_esp32_native.c` 的 `native_idle()` 修改两处：
   - **Core 0 tickless 忙等**：原 `while (!tick_timer_pending())` 改为 `while (!tick_timer_pending() && !g_need_resched)`，收到 IPI 设置的 `need_resched` 后立即退出。
   - **Core 1 延时**：原单次 `ets_delay_us(1000)` 改为 10 段 × 100μs 的循环，每段检查 `g_need_resched`，使对侧 IPI 能在 ~100μs 内打断 idle。

   *📄 Source: [kern_port_esp32_native.c](../src/kernel/kern_port_esp32_native.c#L178-L214)*

#### 中文伪代码拆解

```
函数 xeros_ipi_reschedule_handler(arg) {
    // a2 = 指向目标核心 need_resched 的指针
    *(uint8_t *)arg = 1
    ret
}

函数 kern_smp_ipi_reschedule(cpu_id) {
    self = 当前核心
    if (cpu_id 非法 或 cpu_id == self) return
    if (目标核心尚未进入调度循环) return

    // ESP32 双核：esp_ipc_isr_call 只能发往“另一核”
    if (cpu_id == (self ^ 1)) {
        esp_ipc_isr_call(xeros_ipi_reschedule_handler,
                         &g_per_cpu[cpu_id].need_resched)
    }
}

函数 ipc_wake_one(队列) {
    task = 从队列取出
    if (task != NULL) {
        task.state = READY
        kern_smp_ipi_reschedule(task.cpu_id)  // 远程任务需要通知目标核
    }
}

函数 native_idle() {
    if (当前核心是 Core 1) {
        // 把 1ms 拆细，让 IPI 快速打断
        for (i = 0; i < 10 且 !g_need_resched; i++) {
            ets_delay_us(100)
        }
        return
    }

    if (当前任务是 idle 任务) {
        下一唤醒 = 计算下一个 SLEEPING 任务唤醒时间
        if (可进入 tickless) {
            重编程 GPTimer
            while (!tick_timer_pending() 且 !g_need_resched) {
                nop
            }
            恢复周期性 tick
            return
        }
    }

    ets_delay_us(1000)
}
```

核心思想：**跨核唤醒不能只改任务状态，还要让目标核心“听见”**。`esp_ipc_isr_call()` 相当于按了一下目标核心的门铃，目标核心从 idle 中出来，下一轮调度就会发现新就绪任务。

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| m5stick-c-native 编译 | `pio run -e m5stick-c-native` | **通过** |
| m5stick-c 编译 | `pio run -e m5stick-c` | **通过** |
| native 单元测试 | `pio test -e native` | **通过（2 个预存失败/异常，与本次改动无关）** |

#### 仍遗留的风险

- ~~**VFS dentry-tree 并发**：`kern_vfs.c` 目录树仍未加全局锁，SMP 下多任务并发访问可能损坏树结构。~~ **已修复**，详见 [VFS Dentry-Tree 并发保护](kernel/vfs-concurrency.md)。
- **IPI 路径未实机充分验证**：当前已通过编译，但双核高负载下的 IPI 时序、中断嵌套、与 tickless 的交互需要在实机上长时间验证。
- **KERN_CPU_ANY 任务的调度均衡**：IPI 目前只通知对侧核心，当前核心是否立即重调度取决于下一个 tick；未来可考虑根据优先级在当前核心也设置 `need_resched`。

---

## VFS Dentry-Tree 并发保护

在 IPI 实现完成后，继续处理 `doc/index.md` 中标记的 VFS dentry-tree 并发风险。完整设计已另文记录于 [VFS Dentry-Tree 并发保护](kernel/vfs-concurrency.md)，本节只记录关键决策与验证结果。

### 问题

`src/kernel/kern_vfs.c` 维护全局 dentry 树 `g_root_dentry` 与 inode 引用计数。多个 Xeros 任务并发调用 `kern_open`、`kern_vfs_unlink`、`kern_dentry_register` 等函数时，可能同时遍历/修改树结构或增减引用计数，导致数据竞争。

### 修复

1. 新增全局自旋锁 `g_vfs_lock`，保护 dentry 树与 inode 引用计数。
2. 将 `path_walk` 重命名为 `path_walk_locked`，将 `kern_inode_ref`/`kern_inode_unref` 重命名为带 `_locked` 后缀版本，明确调用者必须持锁。
3. 对所有公共 VFS API（`kern_dentry_register`、`kern_path_resolve`、`kern_vfs_mkdir`、`kern_vfs_unlink`、`kern_vfs_touch`、`kern_open`、`fd_close_raw`）加锁。
4. `kern_open` 中，先分配 FD 槽位，再持 VFS 锁解析路径并引用 inode，**释放锁后再调用设备 `open` 回调**，避免回调中可能触发的调度导致死锁。

### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| native 编译 | `pio run -e native` | **通过** |
| m5stick-c-native 编译 | `pio run -e m5stick-c-native` | **通过** |
| native 单元测试 | `pio test -e native` | **通过（523/527，2 个预存失败）** |
| 独立验证代理 | 对抗性审查 | **PASS** |

---

### 2026-06-27：修复调度器状态、sleep/yield 竞态、GPTimer 启动与调试工具

#### 发现的问题

1. **`kern_sched_tick` 未设置 `next->state = KERN_TASK_RUNNING`**

   在 ESP32 fallback 分支（含 `XEROS_NATIVE_SCHED`）中，切换到新任务后没有将其状态标记为 RUNNING。结果 `ps` 命令中所有任务永远显示 READY/SLEEP，从未出现 RUNNING；且每次 tick 都因 `g_current_task->state != KERN_TASK_RUNNING` 条件为真而触发无效重调度。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L419-L425)（修复前）*

2. **`kern_sleep_ms` 调用 `kern_yield()` 覆盖 SLEEPING 状态**

   `kern_yield()` 无条件将 `cur->state = KERN_TASK_READY`。`kern_sleep_ms()` 先设置 SLEEPING 再调用 `kern_yield()`，结果 yield 立刻把状态改回 READY，睡眠任务永远不会真正休眠。`wifi-mgr` 任务调用 `kern_sleep_ms(10)` 后实际一直处于 READY，无法进入 SLEEP 从而让 tickless idle 生效。

   *📄 Source: [kern_task_lifecycle.c](../src/kernel/kern_task_lifecycle.c)（修复前）*

3. **GPTimer 周期 tick 从未启动**

   `kern_port_timer_set_periodic()` 定义了完整的 GPTimer 初始化/启动流程，但在 `kern_sched_init()` 的任何分支中都没有被调用。结果是：抢占式 tick 从未生效，`kern_port_preempt_consume()` 始终返回 false；tickless idle 的 `tick_timer_set_next_alarm()` 因 timer 未运行而成为空操作。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L191-L200)（修复前）*

4. **`xeros_debug.py --wait-boot` 启动检测失败**

   三个问题叠加：(a) 启动标记正则只匹配 `[BOOT]`，而实际输出为 `[  BOOT]`（带空格）；(b) `reset()` 使用了 esptool 风格的三级序列（DTR/RTS 交替），在退出时额外触发了下载模式→复位，导致设备二次复位；(c) `main()` 在 `dbg.reset()` 之后 `time.sleep(1)` 才开始读取，期间串口缓冲区被启动日志淹没，`[  BOOT]` 标记被覆盖丢失。

#### 修复

1. 在 `kern_sched.c` ESP32 fallback 分支的 `kern_sched_tick()` 中，`g_current_task = next` 后增加 `next->state = KERN_TASK_RUNNING`。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L423)*

2. 将 `kern_sleep_ms()` 中的 `kern_yield()` 替换为 `kern_port_task_yield()`，直接让出 CPU 而不重置状态。

   *📄 Source: [kern_task_lifecycle.c](../src/kernel/kern_task_lifecycle.c)*

3. 在 `kern_sched_init()` 的 `XEROS_NATIVE_SCHED` 分支中，`kern_port_init()` 之后调用 `kern_port_timer_set_periodic(1000)`，启动 1ms GPTimer tick。

   *📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L193-L197)*

4. 三修 `xeros_debug.py`：
   - 启动正则改为 `r'\[\s*BOOT\s*\]|app_main|Xeros'`。
   - `reset()` 简化为单次 RTS 脉冲（`rts=True, 0.2s, rts=False`），不碰 DTR。
   - 移除 `main()` 中 `reset()` 后的 `time.sleep(1)`，让 `wait_for_boot` 立即开始读取。

   *📄 Source: [xeros_debug.py](../tools/xeros_debug.py#L75-L89)*

#### 验证结果

| 检查项 | 命令 | 结果 |
|--------|------|------|
| native 编译 | `pio run -e native` | **通过** |
| m5stick-c-native 编译 | `pio run -e m5stick-c-native` | **通过** |
| 烧录 | `pio run -e m5stick-c-native -t upload` | **通过** |
| 启动检测 | `python3 tools/xeros_debug.py ... --reset --wait-boot` | **`[OK] 设备已启动 (检测到: [  BOOT] M5Stick-P1 kernel starting...)`** |
| GPTimer 启动 | 启动日志 | **`tick_timer: timer initialized: period=1000 us ... timer started`** |
| Shell `ps` 任务状态 | 串口发送 `ps` | **shell/ui 显示 RUNNING，wifi-mgr 显示 SLEEP** |
| 调试工具端到端 | `--reset --wait-boot --cmd ps` | **正常复位→检测启动→发送命令→解析输出** |

> **See Also:** [原生内核架构](architecture/xeros-native-kernel.md) | [实施计划](../implementation-plan.md) | [FreeRTOS 剩余引用](../freertos-remaining-references.md)
