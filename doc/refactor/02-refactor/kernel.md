# 阶段 2 — Xeros 内核层重构报告

> **Parent:** [重构跟踪](../README.md) | **Prev:** [01-诊断报告](../01-diagnosis.md)

## 目标

统一内核层公共接口的返回类型、降低调度器全局状态的直接暴露、消除跨后端重复初始化逻辑，为后续 HAL / UI / App 层重构提供稳定的内核契约。

## 变更摘要

### 1. 封装调度器全局状态（Task 1）

为 `g_task_list`、`g_task_list_tail`、`g_current_task`、`g_sched_ticks`、`g_need_resched` 提供访问器，避免调度类与任务管理代码直接操作全局变量。

*📄 Source: [kern_sched.h](../../../src/kernel/kern_sched.h)*

新增访问器：

```c
kern_task_t *kern_task_list_head(void);
kern_task_t *kern_task_list_tail(void);
void         kern_task_list_set_head(kern_task_t *task);
void         kern_task_list_set_tail(kern_task_t *task);

kern_task_t *kern_current_task(void);
void         kern_set_current_task(kern_task_t *task);

uint32_t     kern_sched_ticks(void);
void         kern_set_need_resched(bool need);
bool         kern_need_resched(void);
```

*📄 Source: [kern_sched.c](../../../src/kernel/kern_sched.c#L44-L54)*

已将 `kern_sched_rr.c`、`kern_sched_fifo.c` 中的直接全局访问替换为访问器调用。

### 2. 提取公共调度器初始化（Task 2）

将 `kern_sched_init()` 拆分为后端无关的公共辅助函数与后端专用初始化函数：

- `sched_reset_common()`：重置全局状态。
- `sched_register_default_classes()`：注册 RR / FIFO 调度类。
- `sched_idle_create()`：创建 per-CPU idle 任务。
- `freertos_init()` / `esp32_native_init()` / native 分支：各自处理上下文/thread 创建。

*📄 Source: [kern_sched.c](../../../src/kernel/kern_sched.c#L129-L169)*

### 3. 提取公共任务创建流程（Task 3）

将 `kern_spawn()` 中三个后端共有的逻辑提取为：

- `task_create_common()`：分配 TCB、分配 PID、初始化通用字段、命名。
- `task_list_append()`：自旋锁保护下追加到全局任务链表。
- `task_enqueue_to_class()`：按 `scheduler_class_id` 入队到对应调度类。

*📄 Source: [kern_task_lifecycle.c](../../../src/kernel/kern_task_lifecycle.c#L27-L104)*

### 4. 统一定时器接口返回类型为 `kern_err_t`（Task 4）

- `kern_port_ops_t.timer_set_periodic` 与 `kern_port_timer_set_periodic()` 从 `int` 改为 `kern_err_t`。
- 所有后端（FreeRTOS、ESP32 native、native test stub）统一返回 `KERN_OK` / `KERN_ERR` / `KERN_EINVAL`。
- 新增 `period_us == 0` 参数校验，返回 `KERN_EINVAL`。

*📄 Source: [kern_port.h](../../../src/kernel/kern_port.h#L85-L216)*
*📄 Source: [kern_port_freertos.c](../../../src/kernel/kern_port_freertos.c#L75-L420)*
*📄 Source: [kern_port_esp32_native.c](../../../src/kernel/kern_port_esp32_native.c#L223-L254)*

### 5. 同步文档

- 新增 [可移植层文档](../../../doc/kernel/port.md)，记录 `kern_port_timer_set_periodic()` 的契约、参数、返回值与后端实现。
- 更新 [doc/kernel/index.md](../../../doc/kernel/index.md)。

## 新增 / 修改测试

| 测试文件 | 测试名 | 覆盖点 |
|---|---|---|
| `test/test_native/test_kernel_sched.cpp` | `KernelSchedTest.TaskListHeadAccessorMatchesGlobal` | 全局链表访问器正确性 |
| `test/test_native/test_kernel_sched.cpp` | `KernelSchedTest.CurrentTaskAccessorMatchesGlobal` | 当前任务访问器正确性 |
| `test/test_native/test_kernel_sched.cpp` | `KernelSchedTest.NeedReschedAccessorWorks` | 抢占标志访问器读写 |
| `test/test_native/test_kernel_sched.cpp` | `KernelSchedTest.InitIsIdempotent` | 重复初始化幂等 |
| `test/test_native/test_kernel_task.cpp` | `KernelTaskTest.SpawnedTaskHasValidStackAndContext` | 新任务栈与上下文有效 |
| `test/test_native/test_kernel_sched.cpp` | `KernelPortTest.TimerSetPeriodicReturnsOk` | 定时器启动返回 `KERN_OK` |
| `test/test_native/test_kernel_sched.cpp` | `KernelPortTest.TimerSetPeriodicRejectsZeroPeriod` | 零周期返回 `KERN_EINVAL` |

## 验证结果

- `pio test -e native`：533 个用例，2 个跳过，531 个通过。
- `pio run -e m5stick-c`：成功，无新增编译警告。
- `pio run -e m5stick-c-native`：成功，无新增编译警告。

## 风险与后续注意

- 访问器为零开销函数；对性能无影响。
- `kern_err_t` 仍基于 `typedef int`，后续若需更强类型安全可考虑枚举或结构体包装，但会涉及 ABI 与调用约定变更。
- 定时器 `period_us == 0` 的校验为新增行为；调度器始终以 1000us 调用，无回归风险。

## 相关提交

```
4aaf6f2 fix(kernel,test): baseline failures before refactor
a177492 docs(refactor): add baseline report and tracking README
e5ba890 docs(refactor): mark stage 1 as RUNNING
81ec4fb docs(refactor): stage 1 diagnosis report
368f63a refactor(kernel): encapsulate scheduler global state
96c0b49 refactor(kernel): extract common scheduler initialization
b4be007 refactor(kernel): extract common task spawn flow
a6d0d73 refactor(kernel): unify port timer return type with kern_err_t
```

---

> **See Also:** [kernel 可移植层文档](../../../doc/kernel/port.md) | [下一阶段：HAL 层](hal.md)
