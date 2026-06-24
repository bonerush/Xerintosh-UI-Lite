# Xeros 原生内核实施计划

> **Parent:** [知识地图](index.md) | **Related:** [原生内核架构](architecture/xeros-native-kernel.md)

## 概述

本文档描述从 FreeRTOS 后端迁移到 Xeros 原生内核后端的完整实施计划。共 8 个阶段、约 29 个新文件、13 个修改文件。

## 阶段总览

| 阶段 | 名称 | 任务数 | 复杂度 | 依赖 |
|------|------|--------|--------|------|
| 1 | 上下文切换基础 | 7 | 极高 | 无 |
| 2 | 调度器与 Tick 定时器 | 5 | 极高 | 阶段 1 |
| 3 | IPC 原语 | 5 | 高 | 阶段 2 |
| 4 | SMP 原生支持 | 3 | 高 | 阶段 2 |
| 5 | 内存管理层 | 2 | 中 | 阶段 2 |
| 6 | 空闲与低功耗 | 2 | 中 | 阶段 2 |
| 7 | 调试与诊断 | 5 | 中 | 阶段 2 |
| 8 | 构建系统与集成 | 5 | 中 | 全部 |

## 阶段 1: 上下文切换基础

**目标:** 实现 call0 ABI 上下文切换引擎，替代已知有 bug 的 setjmp/longjmp 方案。

### 任务 1.1: 创建 esp32 目录和 ctx_switch.h 头文件

- **文件:** `src/kernel/esp32/ctx_switch.h`
- **复杂度:** 中
- **描述:** 定义 `kern_ctx_native_t` 结构体，包含所有 call0 callee-saved 寄存器（A0-A15）、特殊寄存器（SAR, LBEG, LEND, LCOUNT, PS, EXCCAUSE, EXCVADDR）和保存的 PC。声明四个汇编可调用函数。
- **验证:** 头文件独立编译通过。结构体大小为 24 个 uint32_t 字段 = 96 字节。

### 任务 1.2: 实现 ctx_switch.S 汇编

- **文件:** `src/kernel/esp32/ctx_switch.S`
- **复杂度:** 极高
- **描述:** 编写 Xtensa 汇编实现三个核心上下文切换函数。`xeros_ctx_save`: 保存所有寄存器到 `kern_ctx_native_t`，返回 0 表示保存路径，1 表示恢复路径。`xeros_ctx_restore`: 从结构体加载所有寄存器并跳转到保存的 PC。`xeros_flush_windows`: 执行 `rotw` 指令刷新所有寄存器窗口。
- **关键细节:**
  - 设置 `PS.INTLEVEL` 为 `XCHAL_EXCM_LEVEL` 防止中断破坏
  - 使用 `.section .iram1` 放置热路径到 IRAM
  - `xeros_ctx_save` 使用返回值区分保存/恢复路径
- **验证:** 使用 `xtensa-esp32-elf-as` 汇编无错误。硬件测试：创建两个任务写入不同栈模式，切换 100 次验证无寄存器损坏。

### 任务 1.3: 实现 ctx_init.c

- **文件:** `src/kernel/esp32/ctx_init.c`
- **复杂度:** 高
- **描述:** 实现 `xeros_ctx_init()`，构造新任务的初始栈帧。设置入口为保存的 PC，A0 为退出蹦床（调用 `kern_exit()`），A6（call0 第一个参数）为任务参数。
- **验证:** 硬件测试：初始化上下文后 `xeros_ctx_restore` 跳转执行，验证入口函数正确执行且参数正确。

### 任务 1.4: 修改 kern_task.h

- **文件:** `src/kernel/kern_task.h`, `src/kernel/kern_ctx_esp32.h`
- **复杂度:** 中
- **描述:** 在 TCB 中添加 `native_ctx`（`kern_ctx_native_t*`）和 `native_stack`（`uint8_t*`）字段，由 `#ifdef XEROS_NATIVE_SCHED` 保护。更新 `kern_ctx_esp32.h` 使用新上下文类型替换 setjmp。
- **验证:** FreeRTOS 构建仍编译通过。NATIVE_TEST 构建仍编译通过。

### 任务 1.5: 更新 kern_sched.c

- **文件:** `src/kernel/kern_sched.c`
- **复杂度:** 高
- **描述:** 替换 `XEROS_NATIVE_SCHED` 代码路径中的 setjmp/longjmp 为 `xeros_ctx_save`/`xeros_ctx_restore`。将 `g_sched_ctx` 从 `kern_ctx_t` 改为 `kern_ctx_native_t`。
- **验证:** 硬件测试：idle 任务运行并 yield 成功。两个任务交替执行 1000 次无崩溃。

### 任务 1.6: 更新 kern_task_lifecycle.c

- **文件:** `src/kernel/kern_task_lifecycle.c`
- **复杂度:** 高
- **描述:** 替换 `kern_spawn()`、`kern_yield()`、`kern_exit()`、`kern_sleep_ms()` 中的 setjmp/longjmp 为原生上下文切换。
- **验证:** 创建 3 个任务递增共享计数器并 yield，验证计数器达到预期值。睡眠任务在正确时间唤醒。

### 任务 1.7: 单元测试

- **文件:** `test/test_native/test_ctx_switch.cpp`
- **复杂度:** 中
- **描述:** GoogleTest 测试上下文初始化、save/restore 往返、蹦床函数。
- **验证:** `pio test -e native` 通过所有测试。

---

## 阶段 2: 调度器与 Tick 定时器

**目标:** 实现硬件定时器驱动的抢占式调度，替换 FreeRTOS 双信号量协议。

### 任务 2.1: 实现 tick_timer 模块

- **文件:** `src/kernel/esp32/tick_timer.h`, `src/kernel/esp32/tick_timer.c`
- **复杂度:** 高
- **描述:** 使用 ESP32 TIMG0 实现周期性 tick 中断。ISR 递增 `g_sched_ticks`，检查睡眠任务唤醒时间，设置 `g_need_resched`。支持 tickless idle 的重新编程。
- **验证:** 硬件测试：启动定时器 1ms 周期，验证计数正确。ISR 执行时间 < 10us。

### 任务 2.2: 实现原生后端主文件

- **文件:** `src/kernel/kern_port_esp32_native.c`, `src/kernel/kern_port_esp32_native.h`
- **复杂度:** 极高
- **描述:** 实现完整的 `kern_port_ops_t` 操作表。`switch_to` 使用 `xeros_ctx_save`/`xeros_ctx_restore` 直接切换。`idle` 执行 `waiti 0`。完全消除双信号量协议。
- **验证:** 编译通过。硬件测试：内核启动、idle 运行、任务调度正常。

### 任务 2.3: 更新 ops 表选择

- **文件:** `src/kernel/kern_port.h`, `src/kernel/kern_port_freertos.c`
- **复杂度:** 低
- **描述:** 条件选择 FreeRTOS 或原生 ops 表。移除 `kern_port_freertos.c` 中的桩代码。
- **验证:** 两个构建环境均编译通过，无重复符号。

### 任务 2.4: 更新调度器循环

- **文件:** `src/kernel/kern_sched.c`
- **复杂度:** 中
- **描述:** 为原生后端添加调度器循环，使用 `kern_sched_tick()` + `waiti 0`。
- **验证:** 编译通过，无 FreeRTOS 头文件依赖。

### 任务 2.5: 调度集成测试

- **文件:** `test/test_native/test_sched_native.cpp`
- **复杂度:** 中
- **描述:** 测试抢占、睡眠精度、多任务并发。
- **验证:** 串口日志验证正确调度顺序。

---

## 阶段 3: IPC 原语

**目标:** 实现完整的 IPC 子系统。

### 任务 3.1: 实现信号量和互斥锁

- **文件:** `src/kernel/kern_ipc.h`, `src/kernel/kern_ipc.c`
- **复杂度:** 高
- **描述:** 二值信号量、计数信号量、带优先级继承的互斥锁。所有原语有 `_from_isr` 变体。
- **验证:** 单元测试：信号量 give/take、互斥锁优先级继承。

### 任务 3.2: 实现消息队列

- **文件:** `src/kernel/kern_ipc_queue.h`, `src/kernel/kern_ipc_queue.c`
- **复杂度:** 中
- **描述:** 固定大小环形缓冲区，支持阻塞 send/recv 和 ISR 安全变体。
- **验证:** 单元测试：发送/接收数据完整性。

### 任务 3.3: 实现事件组

- **文件:** `src/kernel/kern_ipc_event.h`, `src/kernel/kern_ipc_event.c`
- **复杂度:** 中
- **描述:** 事件标志位，支持 AND/OR 等待和自动清除。
- **验证:** 单元测试：AND/OR 等待、超时。

### 任务 3.4: 迁移现有用户

- **文件:** `src/kernel/kern_sync.h`, `src/kernel/kern_sync.c`, `src/kernel/kern_port_freertos.c`
- **复杂度:** 低
- **描述:** 将 `portMUX_TYPE` 用户迁移到原生 spinlock。
- **验证:** 两个构建均编译通过。

### 任务 3.5: IPC 集成测试

- **文件:** `test/test_native/test_ipc_native.cpp`
- **复杂度:** 中
- **描述:** 优先级反转场景测试。
- **验证:** 优先级继承正确触发，完成时间符合预期。

---

## 阶段 4: SMP 原生支持

**目标:** 替换 FreeRTOS 依赖的 SMP 代码。

### 任务 4.1: 实现 smp_native 模块

- **文件:** `src/kernel/esp32/smp_native.h`, `src/kernel/esp32/smp_native.c`
- **复杂度:** 高
- **描述:** 原生 CPU ID 查询（直接读寄存器）、IPI 实现、核心启动。
- **验证:** 硬件测试：两个核心正确识别和启动。

### 任务 4.2: 更新 kern_smp.c

- **文件:** `src/kernel/kern_smp.c`, `src/kernel/kern_smp.h`
- **复杂度:** 高
- **描述:** 移除 `xPortGetCoreID` 和 `xTaskCreatePinnedToCore` 依赖。
- **验证:** 原生构建无 FreeRTOS SMP 依赖。

### 任务 4.3: SMP 测试

- **文件:** `test/test_native/test_smp_native.cpp`
- **复杂度:** 中
- **描述:** 双核调度、跨核信号量唤醒测试。
- **验证:** 任务在正确核心执行，无死锁。

---

## 阶段 5: 内存管理层

**目标:** 适配内存管理不依赖 ESP-IDF heap_caps。

### 任务 5.1: 实现 mem_native 模块

- **文件:** `src/kernel/esp32/mem_native.h`, `src/kernel/esp32/mem_native.c`
- **复杂度:** 中
- **描述:** 封装 ESP-IDF multi_heap 模块（不依赖 FreeRTOS）。
- **验证:** 堆信息正确报告。

### 任务 5.2: 更新 kern_kmalloc.c

- **文件:** `src/kernel/kern_kmalloc.c`, `src/kernel/kern_kmalloc.h`
- **复杂度:** 低
- **描述:** 条件编译支持原生内存管理。
- **验证:** 分配/释放循环正常。

---

## 阶段 6: 空闲与低功耗

**目标:** 实现空闲任务和低功耗模式。

### 任务 6.1: 实现 waiti 空闲

- **文件:** `src/kernel/kern_port_esp32_native.c`
- **复杂度:** 中
- **描述:** idle 任务执行 `waiti 0` 进入低功耗状态。
- **验证:** 功耗测量验证低功耗状态。

### 任务 6.2: 实现 tickless idle

- **文件:** `src/kernel/kern_port_esp32_native.c`, `src/kernel/esp32/tick_timer.c`
- **复杂度:** 高
- **描述:** 无任务就绪时延长 tick 间隔。
- **验证:** 功耗测量验证深度睡眠。

---

## 阶段 7: 调试与诊断

**目标:** 实现调试基础设施。

### 任务 7.1: 实现调试框架

- **文件:** `src/kernel/debug/kern_debug.h`, `src/kernel/debug/kern_debug.c`
- **复杂度:** 中
- **描述:** 调试级别控制、条件编译开关。
- **验证:** 关闭调试时无额外代码。

### 任务 7.2: 实现任务检查器

- **文件:** `src/kernel/debug/kern_task_inspector.c`
- **复杂度:** 低
- **描述:** 扩展 `/proc/tasks` 显示详细信息。
- **验证:** shell `ps` 命令输出正确。

### 任务 7.3: 实现调度追踪

- **文件:** `src/kernel/debug/kern_sched_trace.c`
- **复杂度:** 中
- **描述:** 环形缓冲区记录调度事件。
- **验证:** 运行 10 秒后 `trace` 命令显示正确记录。

### 任务 7.4: 实现内存分析器

- **文件:** `src/kernel/debug/kern_mem_profiler.c`
- **复杂度:** 中
- **描述:** Per-task 内存分配追踪。
- **验证:** `meminfo` 显示正确的 per-task 统计。

### 任务 7.5: 实现 IPC 争用日志

- **文件:** `src/kernel/debug/kern_debug.c`
- **复杂度:** 低
- **描述:** 记录 mutex 等待时间和优先级继承事件。
- **验证:** `ipc` 命令显示争用统计。

---

## 阶段 8: 构建系统与集成

**目标:** 配置构建系统，完成全系统集成测试。

### 任务 8.1: 更新 platformio.ini

- **文件:** `platformio.ini`
- **复杂度:** 低
- **描述:** 添加 `m5stick-c-native` 构建环境。
- **验证:** `pio run -e m5stick-c-native` 编译成功。

### 任务 8.2: 更新 CMakeLists.txt

- **文件:** `src/CMakeLists.txt`
- **复杂度:** 中
- **描述:** 条件编译支持。
- **验证:** 两个环境无重复符号。

### 任务 8.3: 创建 sdkconfig.native

- **文件:** `sdkconfig.native`
- **复杂度:** 低
- **描述:** 原生后端专用配置。
- **验证:** 构建和启动正常。

### 任务 8.4: 全系统集成测试

- **复杂度:** 高
- **描述:** 完整应用运行 5 分钟无崩溃。所有 shell 命令正常。
- **验证:** 无 WDT 重置、无内存泄漏。

### 任务 8.5: 性能基准测试

- **文件:** `test/test_native/test_benchmark.cpp`
- **复杂度:** 中
- **描述:** 上下文切换延迟、IPC 吞吐量、调度延迟基准。
- **验证:** 结果记录并对比 FreeRTOS 后端。

---

## 风险评估

| 风险 | 严重度 | 缓解措施 |
|------|--------|----------|
| 上下文切换时寄存器窗口损坏 | 严重 | 切换前调用 `xeros_flush_windows()`，栈金丝雀验证 |
| ESP-IDF 系统服务 (WiFi/BT) 中断 | 高 | 保持 FreeRTOS idle 任务运行处理系统事务 |
| 定时器中断与 ESP-IDF 冲突 | 高 | 使用 TIMG0（IDF 使用 SYSTIMER） |
| 原生 IPC 死锁 | 高 | 优先级继承 + 死锁检测 |
| 原生任务栈溢出 | 高 | MPU 栈保护 + 金丝雀检查 |
| 次级核心启动时序 | 中 | volatile ready 标志 + 忙等待 |
| FreeRTOS idle 任务饥饿 | 中 | idle 调用 waiti 允许 FreeRTOS 运行 |
| ESP32 硅片版本差异 | 低 | 多版本测试 |

## 测试策略

### 第 1 层：原生平台单元测试 (GoogleTest)

```
pio test -e native
```

测试内核逻辑，不依赖硬件：
- 上下文切换模拟测试
- IPC 原语逻辑测试
- 调度器类测试

### 第 2 层：硬件集成测试

```
pio run -e m5stick-c-native -t upload
python3 tools/xeros_debug.py /dev/ttyUSB0
```

在真实 ESP32 上验证：
- 上下文切换正确性
- 定时器精度
- IPC 功能
- SMP 调度

### 第 3 层：性能基准

```
pio test -e m5stick-c-native --filter test_benchmark
```

测量并记录：
- 上下文切换延迟
- IPC 操作延迟
- 调度延迟
- 内存分配延迟

### 第 4 层：长时间稳定性

```
# 运行 24 小时
python3 tools/xeros_debug.py /dev/ttyUSB0 --duration 86400
```

验证：
- 无内存泄漏
- 无 WDT 重置
- 无优先级反转死锁
- 栈使用稳定

---

> **See Also:** [原生内核架构](architecture/xeros-native-kernel.md) | [知识地图](index.md)
