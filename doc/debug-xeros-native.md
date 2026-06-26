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

#### 2.1 看门狗喂狗

`src/main.cpp:289` 显式调用：

```cpp
vTaskDelay(pdMS_TO_TICKS(1));
```

*📄 Source: [main.cpp](../src/main.cpp#L289)*

**原因**: Xeros 调度器任务优先级为 `tskIDLE_PRIORITY + 1`，高于 FreeRTOS idle（0）。若调度器长时间不 yield，idle 任务无法在 300ms 内喂 INT_WDT，导致复位。`vTaskDelay(1)` 强制让出 CPU 给 idle。

`sdkconfig.m5stick-c-native` 中已关闭 `CONFIG_ESP_INT_WDT`，为原生调度器实验留出空间。

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

### 3. 当前 XEROS_NATIVE_SCHED 的真实状态

- `ctx_switch.S` 和 `ctx_init.c` 被 `#if !defined(NATIVE_TEST) && !defined(XEROS_NATIVE_SCHED)` 排除。
- `kern_port_native.c` 被 `#ifndef XEROS_NATIVE_SCHED` 排除。
- TCB 没有 `native_ctx` / `native_stack` 字段。
- 因此 `m5stick-c-native` 实际仍走 FreeRTOS 双信号量后端。

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

（后续在此追加）

---

## Phase 2 实施记录

（后续在此追加）

---

> **See Also:** [原生内核架构](architecture/xeros-native-kernel.md) | [实施计划](../implementation-plan.md) | [FreeRTOS 剩余引用](../freertos-remaining-references.md)
