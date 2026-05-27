# 内核可移植层 (`kern_port`)

> 源码: `src/kernel/kern_port.h`, `src/kernel/kern_port.c`, `src/kernel/kern_port_native.c`

## 概述

`kern_port` 是 Xeros 内核的**底层执行上下文抽象层**，将所有平台相关的线程创建、上下文切换、栈管理操作隔离在单一模块中。

内核其他模块（`kern_task` 等）仅通过 `kern_port.h` 接口调用，不直接依赖 FreeRTOS 或任何特定调度原语。

## 设计目标

1. **零 FreeRTOS 渗透**：除 `kern_port.c` 外，内核所有源文件不包含任何 FreeRTOS 头文件
2. **双后端可切换**：默认 FreeRTOS 任务容器 / 实验性原生 setjmp/longjmp
3. **编译期选择**：通过宏定义切换后端，无运行时开销

## API

| 函数 | 方向 | 说明 |
|------|------|------|
| `kern_port_init()` | - | 初始化可移植层 |
| `kern_port_thread_spawn()` | - | 创建新的执行上下文 |
| `kern_port_thread_exit()` | 任务→调度器 | 销毁当前线程（不返回） |
| `kern_port_thread_stack_usage()` | - | 获取栈使用量 |
| `kern_port_switch_to(task)` | 调度器→任务 | 调度器切换到目标任务（阻塞等待） |
| `kern_port_task_yield()` | 任务→调度器 | 任务主动让出 CPU |
| `kern_port_task_exit()` | 任务→调度器 | 任务退出并销毁（不返回） |
| `kern_port_idle()` | - | 无就绪任务时的空闲处理 |

## 后端对比

| 特性 | FreeRTOS (`kern_port.c`) | 原生 (`kern_port_native.c`) |
|------|--------------------------|---------------------------|
| 线程模型 | 1 个 Xeros 任务 = 1 个 FreeRTOS 任务 | 单线程 + 协程 |
| 栈管理 | FreeRTOS 自动管理 | 手动 malloc + 金丝雀检测 |
| 上下文切换 | 双二值信号量 give/take | setjmp/longjmp |
| FreeRTOS 依赖 | 本文件独占（xTaskCreate/vTaskDelete/xSemaphore） | **零** |
| 稳定性 | ✅ 生产可用 | ⚠️ 实验性（Xtensa ABI 风险） |
| 启用方式 | 默认（`#else` 路径） | `-DXEROS_NATIVE_SCHED` |

## 协议说明（FreeRTOS 后端）

```
调度器（loop）                任务（wrapper）
─────────────                ───────────────
                              take(token) ← 阻塞等令牌
kern_sched_tick()
  pick_next_ready()
  kern_port_switch_to(task)
    give(token) ──────────→  获得令牌
    take(done) ← 阻塞        task->entry(arg)
                              give(done) ──→ 调度器解除阻塞
                              take(token) ← 再次阻塞
  take(done) 返回
  下一轮 tick...
```

## FreeRTOS 依赖收敛

去 FreeRTOS 前后的依赖分布对比：

```
Before (Phase 0-5):
  kern_init.c   ██████  6 处 FreeRTOS API
  kern_task.c   ████████████████████████  27 处
  kern_task.h   ███  3 处

After (Phase 6):
  kern_port.c   ██████████████████████  所有 FreeRTOS 集中于此
  kern_init.c   0 处 ✅
  kern_task.c   0 处 ✅
  kern_task.h   0 处 ✅
```

**现在整个内核只有一个文件 (`kern_port.c`) 直接依赖 FreeRTOS**。切换到原生调度器时替换此文件即可。

## 启用原生调度器

```ini
; platformio.ini
build_flags = -DXEROS_NATIVE_SCHED
```

⚠️ **风险提示**：原生调度器使用 setjmp/longjmp 在 Xtensa 架构上切换上下文。由于 Xtensa 的窗口寄存器 ABI（call8/call12 的父窗口可能丢失），此模式可能不稳定。建议仅在开发板上充分测试后使用。
