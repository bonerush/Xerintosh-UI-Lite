# Phase 6: 去 FreeRTOS 解耦计划

> 创建日期: 2026-05-28
> 状态: 计划阶段
> 关联: Phase 0-5（已完成）

---

## 1. 现状审计结论

### FreeRTOS 依赖分布

```
                     FreeRTOS API 调用点
┌──────────────────────────────────────────────┐
│ kern_task.c    ██████████████████████  27     │  ← 核心调度器（可替换）
│ kern_task.h    ███                      3     │  ← TaskHandle_t 类型（可替换）
│ kern_init.c    ██████                   6     │  ← 日志互斥锁（可替换）
│                                              │
│ app/ 层        0  ✅ 已完全解耦               │
│ hal/ 层        0  ✅ 已完全解耦               │
│ ui/  层        0  ✅ 已完全解耦               │
│ main.cpp       0  ✅ 仅注释提及               │
│                                              │
│ ESP-IDF 内部   ████████████████████  N/A      │  ← WiFi/BT 协议栈（不可移除）│
└──────────────────────────────────────────────┘
```

**合计 36 处可替换的 FreeRTOS 调用，全部在 #else (ESP32) 条件编译路径内。**

### 硬约束

WiFi（`esp_wifi`）和蓝牙（NimBLE）协议栈是 ESP-IDF 的二进制组件，深度依赖 FreeRTOS。**从固件中彻底移除 FreeRTOS 库是不可能的**，但可以让我们的内核代码不再直接调用它。

---

## 2. 当前架构 vs 目标架构

### 当前（FreeRTOS 任务容器模型）

```
loop()
  └── kern_sched_tick()
        ├── xSemaphoreGive(token)  ← FreeRTOS API
        ├── xSemaphoreTake(done)   ← FreeRTOS API
        └── [FreeRTOS 调度器选择下一个 Xeros 任务]
              ├── shell  (xTaskCreate 创建的 FreeRTOS task)
              ├── ui     (xTaskCreate 创建的 FreeRTOS task)
              ├── wifi   (xTaskCreate 创建的 FreeRTOS task)
              └── bt     (xTaskCreate 创建的 FreeRTOS task)

每个 Xeros 任务 = 1 个 FreeRTOS 任务
任务切换 = 双信号量 give/take 协议
```

### 目标（Xeros 原生调度器模型）

```
loop()
  └── kern_sched_tick()
        └── [Xeros 调度器直接 longjmp 到下一个 Xeros 任务]
              ├── shell  (纯 setjmp/longjmp 上下文)
              ├── ui     (纯 setjmp/longjmp 上下文)
              ├── wifi   (纯 setjmp/longjmp 上下文)
              └── bt     (纯 setjmp/longjmp 上下文)

所有 Xeros 任务运行在 Arduino loop() 的单一线程内
无 FreeRTOS 任务创建，无信号量
```

---

## 3. 实施策略：三步渐进式解耦

### Step 1: 移除 kern_init.c 日志互斥锁（6 处）—— 安全，无风险

**现状**: `kern_init.c` 用 `SemaphoreHandle_t g_log_mutex` 保护 `kern_log()` 的串口输出。

**问题**: 协作式调度下 Xeros 任务不会互相抢占，唯一并发来自 WiFi/BT 协议栈的 FreeRTOS 任务。

**方案**: 替换为原子自旋锁（`portENTER_CRITICAL` / `portEXIT_CRITICAL` 或纯 volatile 标志）。

```c
// Before (FreeRTOS)
static SemaphoreHandle_t g_log_mutex;
xSemaphoreTake(g_log_mutex, portMAX_DELAY);
// ... printf ...
xSemaphoreGive(g_log_mutex);

// After (无 FreeRTOS 依赖)
static volatile bool g_log_locked = false;
while (__sync_lock_test_and_set(&g_log_locked, true)) { /* spin */ }
// ... printf ...
__sync_lock_release(&g_log_locked);
```

**影响**: 0 运行时行为变化。移除 6 处 FreeRTOS 调用，删除 `#include <freertos/semphr.h>`。

---

### Step 2: 创建内核可移植层 `kern_port.h` —— 架构抽象

**目标**: 把所有 FreeRTOS 类型和 API 调用隔离到单一文件中，使 `kern_task.c` 不直接 include FreeRTOS 头文件。

**新建 `src/kernel/kern_port.h`**:

```c
/* ═══ 内核可移植层 ═══ */

#ifdef XEROS_NATIVE_SCHED
  /* 原生调度器：setjmp/longjmp，零 FreeRTOS */
  #include "kern_ctx_esp32.h"
  typedef kern_ctx_t kern_port_ctx_t;
  #define KERN_PORT_STACK_MIN  4096
  // ... 原生原语 ...

#else
  /* 兼容模式：FreeRTOS 任务容器 */
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <freertos/semphr.h>
  typedef TaskHandle_t kern_port_thread_t;
  #define KERN_PORT_STACK_MIN  4096
  kern_port_thread_t kern_port_thread_create(...);
  void kern_port_thread_delete(kern_port_thread_t h);
  void kern_port_yield(void);
  void kern_port_exit(void);
  // ... FreeRTOS 封装原语 ...
#endif
```

**对 kern_task.c 的改动**:
- 删除 `#include <freertos/*>`，替换为 `#include "kern_port.h"`
- `TaskHandle_t rtos_handle` → `kern_port_thread_t port_handle`
- `xTaskCreate(...)` → `kern_port_thread_create(...)`
- `vTaskDelete(NULL)` → `kern_port_exit()`
- 双信号量 `give/take` → `kern_port_yield()` / `kern_port_switch_to(next)`

**优势**: 
- `kern_task.c` 不再包含任何 FreeRTOS 头文件
- 两个后端（FreeRTOS / 原生）可编译期切换
- 为未来完全去掉 FreeRTOS 提供干净的移植点

---

### Step 3: 完善 `XEROS_NATIVE_SCHED` 原生调度器 —— 去 FreeRTOS 的最终形态

**现状**: `kern_ctx_esp32.h` 已实现 Xtensa `setjmp`/`longjmp` + 手动 SP 切换原语，标注"实验性"。

**待解决的关键问题**:

| 问题 | 说明 | 优先级 |
|------|------|--------|
| 窗口寄存器溢出 | Xtensa 有 64 个物理寄存器，窗口溢出/下溢由硬件处理，但 setjmp 只保存当前窗口（16 个寄存器），可能丢失 call4/call8/call12 的父窗口上下文 | 🔴 高 |
| 栈管理 | 每个 Xeros 任务需要独立栈，`longjmp` 后 SP 切换到目标栈。需确保栈对齐（16 字节）且不会与 FreeRTOS 系统栈冲突 | 🔴 高 |
| 中断安全 | ESP32 有两级中断，`longjmp` 在中断上下文中调用可能导致寄存器状态不一致。需在 `portENTER_CRITICAL` 保护下切换 | 🟡 中 |
| 空闲任务 | 当所有 Xeros 任务都 sleep 时，需要 idle 任务占位。原生模式下 idle 在主栈上 run，通过 `esp_timer` 唤醒 sleep 任务 | 🟡 中 |

**稳定化方案**:

1. **用 `esp_timer` 替代 FreeRTOS 延时**: `kern_sleep_ms()` 注册一次性定时器，到期后标记任务为 READY
2. **单 FreeRTOS 任务 + 协程模型**: 创建一个轻量 FreeRTOS 任务作为"内核线程"，所有 Xeros 任务在其内部通过 longjmp 切换
3. **栈金丝雀保护**: 每个 Xeros 任务栈底部写入 canary，在 yield 前检查溢出

```c
// kern_port.h (XEROS_NATIVE_SCHED 分支)
// 单一线程运行所有 Xeros 任务

void kern_port_sched_loop(void) {
    while (1) {
        kern_task_t *next = pick_next_ready();
        if (next == NULL) {
            // 所有任务都在 sleep：等待定时器
            esp_timer_dispatch_pending();
            continue;
        }
        g_current_task = next;
        next->state = KERN_TASK_RUNNING;
        // 保存调度器上下文，跳转到目标任务
        if (setjmp(g_sched_ctx) == 0) {
            longjmp(next->ctx, 1);
        }
    }
}
```

---

## 4. 实施路线图

| Step | 内容 | 代码变更 | FreeRTOS 消除 | 风险 |
|------|------|---------|---------------|------|
| Step 1 | 日志锁替换 | ~15 行改动 | -6 处 | 🟢 零风险 |
| Step 2 | 创建 kern_port.h 抽象层 | ~120 行新增 | kern_task.c 不再直接 include FreeRTOS | 🟡 低风险（纯重构） |
| Step 3a | 实现 kern_port.c (FreeRTOS 后端) | ~80 行新增 | 封装 FreeRTOS 到单一文件 | 🟡 低风险（语义等价） |
| Step 3b | 实现 kern_port_native.c (原生后端) | ~200 行新增 | 全部 30 处 FreeRTOS 调用消除 | 🔴 高风险（需 Xtensa ABI 验证） |
| Step 4 | 文档更新 + 提交 | ~50 行 | - | 🟢 |

---

## 5. 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/kernel/kern_init.c` | 修改 | 日志锁替换为原子自旋锁 |
| `src/kernel/kern_port.h` | 新建 | 内核可移植层头文件（双后端声明） |
| `src/kernel/kern_port.c` | 新建 | FreeRTOS 兼容后端实现 |
| `src/kernel/kern_port_native.c` | 新建 | 原生调度器后端（setjmp/longjmp） |
| `src/kernel/kern_task.h` | 修改 | `TaskHandle_t` → `kern_port_thread_t`；移除 FreeRTOS include |
| `src/kernel/kern_task.c` | 修改 | 所有 xTaskCreate/xSemaphore → kern_port_* 调用 |
| `src/kernel/kern_ctx_esp32.h` | 修改 | 窗口寄存器保存增强（call12 ABI 兼容） |
| `doc/kernel/kern-port.md` | 新建 | 可移植层文档 |

---

## 6. 不能去掉的部分

以下 FreeRTOS 依赖 **不可移除**，因为它们是 ESP-IDF 二进制组件的要求：

| 组件 | FreeRTOS 用途 | 影响范围 |
|------|--------------|---------|
| `esp_wifi` | WiFi 栈内部使用 FreeRTOS 任务处理 802.11 帧 | 不影响我们的内核代码 |
| NimBLE | 蓝牙协议栈事件循环运行在 FreeRTOS 任务中 | 不影响我们的内核代码 |
| `esp_timer` | 定时器回调在 FreeRTOS 定时器任务中触发 | 可用于替代 vTaskDelay |
| `M5Unified` | `M5.update()` 内部可能使用 FreeRTOS 延时 | 不影响 |

这些组件的 FreeRTOS 依赖对我们是 **透明** 的——我们只需要在固件中链接 FreeRTOS 库，但不需要在我们的代码中调用它。

---

*本计划待用户确认后按 Step 1→2→3 顺序实施。Step 3b（原生调度器）是高风险的实验性步骤，建议先在开发板上单独验证。*
