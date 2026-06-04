# 内核 v2 分析报告与分模块执行计划

> 生成日期: 2026-06-04
> 分支: feature/kernel-v2
> 当前内核版本: 0.2.0 (Xeros)
> 目标: 协作式 → 抢占式 + SMP + MPU + 资源追踪 + 可插拔调度器 + 统一设备驱动模型

---

## 第一部分：当前内核缺陷分析

### 1. 调度器缺陷

| # | 缺陷 | 严重度 | 位置 | 影响 |
|---|------|--------|------|------|
| S1 | **纯协作式，无抢占** | 🔴 严重 | kern_task.c:946 `pick_next_ready()` | 一个任务死循环或长时间计算会导致整个系统无响应。5秒超时保护（kern_port.c:152）只是暴力标记ZOMBIE，不优雅。 |
| S2 | **优先级字段闲置** | 🟡 中等 | kern_task.h:64 `priority` | 定义了 `uint8_t priority`，所有用户任务硬编码为128，idle为0，调度器从未读取。 |
| S3 | **调度算法硬编码** | 🟡 中等 | kern_task.c:946-1039 | Round-Robin逻辑写死在 `pick_next_ready()` 中，无法替换或扩展。 |
| S4 | **O(n)线性扫描** | 🟢 轻微 | kern_task.c:990-1010 | 每次tick遍历整个链表查找READY任务。n≤16所以当前不严重，但架构上不优。 |
| S5 | **BLOCKED状态未实现** | 🟡 中等 | kern_types.h:54 | 枚举定义了 `KERN_TASK_BLOCKED`，但内核中无任何阻塞原语（无mutex/semaphore/条件变量）。 |
| S6 | **无SMP支持** | 🔴 严重 | 全局 | 单核设计。ESP32-PICO是双核（PRO CPU + APP CPU），第二个核完全闲置。 |
| S7 | **时间精度低** | 🟢 轻微 | kern_task.c:82 `g_sched_ticks` | tick计数用作时间基准，但每个loop()迭代才+1，实际精度受帧率限制（~16ms），非毫秒级。 |
| S8 | **任务隔离依赖FreeRTOS** | 🟡 中等 | kern_port.c | 任务栈隔离由FreeRTOS xTaskCreate保障，但Xeros自身的上下文切换协议不提供隔离。切换到原生调度器后将丧失这一点。 |

### 2. 内存保护缺陷

| # | 缺陷 | 严重度 | 位置 | 影响 |
|---|------|--------|------|------|
| M1 | **无MPU内存保护** | 🔴 严重 | 全局 | ESP32-PICO有MPU硬件但未启用。任何任务的野指针都可能破坏内核数据或其他任务的内存。 |
| M2 | **栈溢出被动检测** | 🟡 中等 | kern_task.c:226, 908 | Native下每500 tick扫描0xAA金丝雀，FreeRTOS下查询uxTaskGetStackHighWaterMark。发现时仅打印警告，不阻止崩溃。 |
| M3 | **全局变量无保护** | 🟡 中等 | kern_task.c:77-84 | `g_task_list`, `g_current_task`, `g_last_picked` 等全局变量无任何保护。一个任务的错误写入可能破坏调度器状态。 |
| M4 | **VFS数据结构无保护** | 🟡 中等 | kern_vfs.c | dentry/inode/fd_table全部为全局静态分配，无内存隔离。 |

### 3. 资源管理缺陷

| # | 缺陷 | 严重度 | 位置 | 影响 |
|---|------|--------|------|------|
| R1 | **TCB无资源列表** | 🔴 严重 | kern_task.h:43-74 | 任务持有的锁、动态内存、打开的文件描述符均不追踪。任务被kill时无自动清理。 |
| R2 | **VFS inode泄漏** | 🟡 中等 | kern_vfs.c:210 | `kern_vfs_unlink()` 调用 `free(dentry)` 但不释放 `dentry->inode`。因为inode由注册方管理（静态分配），当前未泄漏，但架构不健全。 |
| R3 | **全局fd表** | 🟡 中等 | kern_vfs.c:26 | `g_fd_table[8]` 为全局单例，所有任务共享。任务A打开的fd对任务B可见（虽然当前单线程无竞争）。 |
| R4 | **VFS对象无引用计数** | 🟡 中等 | kern_vfs.h:50,60 | inode和dentry没有refcount字段。无法安全共享或延迟释放。 |
| R5 | **kern_task_kill后TCB存在竞态** | 🟡 中等 | kern_task.c:555-656 | kill后标记ZOMBIE并vTaskDelete底层FreeRTOS任务，但TCB的free推迟到reap_zombies。如果任务正持有共享资源，这些资源不会被释放。 |

### 4. 设备驱动模型缺陷

| # | 缺陷 | 严重度 | 位置 | 影响 |
|---|------|--------|------|------|
| D1 | **无统一设备模型** | 🟡 中等 | devices/ | 每个设备手动实现自己的fops和注册逻辑。无 `struct device` 抽象、无驱动绑定/解绑、无probe机制。 |
| D2 | **中断处理无框架** | 🟡 中等 | 全局 | GPIO中断、定时器中断等完全未使用。所有输入通过轮询（hal_input轮询GPIO，dev_ttyS0_poll轮询Serial）。 |
| D3 | **dev_fb0 ioctl SET_ROTATION未实现** | 🟢 轻微 | dev_fb0.c:76-83 | 定义了ioctl命令但函数体为空（TODO）。 |
| D4 | **GPIO方向查询始终返回INPUT** | 🟢 轻微 | kern_gpiofs.c:83 | `gpio_get_dir()` 硬编码返回0，Arduino无查询方向的API。 |
| D5 | **GPIO引脚表硬编码** | 🟢 轻微 | kern_gpiofs.c:31-41 | M5Stick-C的7个引脚硬编码在数组中，不支持运行时发现。 |

### 5. 架构/代码质量缺陷

| # | 缺陷 | 严重度 | 位置 | 影响 |
|---|------|--------|------|------|
| A1 | **kern_task.c过大** | 🟡 中等 | kern_task.c (1042行) | 单文件混合了调度器逻辑、任务生命周期、栈管理、僵尸回收、平台条件编译。应拆分。 |
| A2 | **三重条件编译** | 🟡 中等 | kern_task.c 多处 | `#ifdef NATIVE_TEST` / `#elif XEROS_NATIVE_SCHED` / `#else` 导致代码路径难以理解和测试。 |
| A3 | **文档-代码不一致** | 🟡 中等 | doc/kernel/ vs src/kernel/ | 文档描述双向链表、fd_table、动态栈增长等特性，实际代码为单向链表、无这些特性。 |
| A4 | **kern_shell_cmds.c过大** | 🟢 轻微 | kern_shell_cmds.c (971行) | 30+命令的实现集中在一个文件。 |
| A5 | **错误码体系不完整** | 🟢 轻微 | kern_types.h | 定义了14个错误码，但许多函数仅返回 -1 (KERN_ERR)，丢失了错误语义。 |
| A6 | **kern_port_native.c已弃用** | 🟢 轻微 | kern_port_native.c (203行) | `XEROS_NATIVE_SCHED` 未被任何构建配置启用，代码保留但未测试。 |

### 6. 缺陷汇总矩阵

| 类别 | 🔴 严重 | 🟡 中等 | 🟢 轻微 |
|------|---------|---------|---------|
| 调度器 | S1 (协作式), S6 (无SMP) | S2, S3, S5, S8 | S4, S7 |
| 内存保护 | M1 (无MPU) | M2, M3, M4 | - |
| 资源管理 | R1 (无资源追踪) | R2, R3, R4, R5 | - |
| 设备驱动 | - | D1, D2 | D3, D4, D5 |
| 架构/代码 | - | A1, A2, A3 | A4, A5, A6 |

---

## 第二部分：分模块执行计划

### 总体架构

```
Phase 1: 基础重构（不改变外部行为）
  ├── Module 1.1: 拆分 kern_task.c
  ├── Module 1.2: 统一平台抽象层
  └── Module 1.3: 建立内核测试基础设施

Phase 2: 抢占式调度器
  ├── Module 2.1: 可插拔调度器接口
  ├── Module 2.2: 定时器中断驱动
  └── Module 2.3: 优先级调度类实现

Phase 3: SMP 多核支持
  ├── Module 3.1: Per-CPU 数据结构
  ├── Module 3.2: 跨核任务迁移
  └── Module 3.3: SMP 同步原语

Phase 4: MPU 内存保护
  ├── Module 4.1: MPU 区域配置
  ├── Module 4.2: MemManage 异常处理
  └── Module 4.3: 任务优雅重启

Phase 5: 资源追踪与安全终止
  ├── Module 5.1: TCB 资源列表
  ├── Module 5.2: 统一内存分配器
  └── Module 5.3: 安全终止流程

Phase 6: 统一设备驱动模型
  ├── Module 6.1: struct device + driver 框架
  ├── Module 6.2: 中断管理框架
  └── Module 6.3: 现有设备迁移
```

---

### Phase 1: 基础重构

**目标**: 为后续大改动建立干净的基础，不改变外部行为，所有现有测试通过。

#### Module 1.1: 拆分 kern_task.c

**文件**: `src/kernel/kern_task.c` (1042行 → 多个文件)

| 新文件 | 职责 | 估计行数 |
|--------|------|----------|
| `kern_sched.c` | 调度器核心（pick_next_ready, sched_tick, idle, 全局状态） | ~300 |
| `kern_task_lifecycle.c` | 任务生命周期（spawn, exit, kill, reap_zombies） | ~250 |
| `kern_task_stack.c` | 栈管理（分配, 金丝雀, 使用率检测） | ~100 |
| `kern_task_virtual.c` | 虚任务管理 | ~80 |
| `kern_task.c` (保留) | 对外的统一入口 + 公共逻辑 | ~150 |

**不变**: 所有公开API头文件 `kern_task.h` 声明不变。

**验证**: `pio test -e native` 全部通过（当前186/187，末尾SIGSEGV需在Module 1.3中修复）。

#### Module 1.2: 统一平台抽象层

**目标**: 消除三重 `#ifdef`，建立单一平台抽象接口。

**方案**: 将 `kern_port.h` 扩展为完整的平台抽象层，包含：

```c
// kern_port.h — 平台抽象接口 (新)
typedef struct kern_port_ops {
    // 线程操作
    kern_port_thread_t (*thread_spawn)(...);
    void (*thread_exit)(void);
    void (*thread_kill)(kern_port_thread_t);
    
    // 上下文切换
    void (*switch_to)(kern_task_t *task);
    void (*task_yield)(void);
    void (*task_exit)(void);
    void (*idle)(void);
    
    // 内存保护 (Phase 4 使用)
    int  (*mpu_configure)(...);
    
    // 定时器 (Phase 2 使用)
    int  (*timer_set_periodic)(uint32_t us, void (*callback)(void));
    
    // 同步原语 (Phase 3 使用)
    void *(*mutex_create)(void);
    void  (*mutex_lock)(void *m);
    void  (*mutex_unlock)(void *m);
    
    // 诊断
    size_t (*stack_usage)(kern_port_thread_t);
} kern_port_ops_t;

// 编译时选择后端
extern const kern_port_ops_t g_kern_port;
```

**后端实现**:
- `kern_port_freertos.c` — 当前FreeRTOS双信号量实现（重命名自kern_port.c）
- `kern_port_native.c` — 重构setjmp/longjmp实现，用于native测试
- 未来: `kern_port_baremetal.c` — 纯裸机实现（Phase 2+ 产出）

**验证**: `pio test -e native` + `pio run -e m5stick-c` 编译通过。

#### Module 1.3: 内核测试基础设施

**目标**: 修复native测试SIGSEGV，增加内核模块独立测试。

| 操作 | 说明 |
|------|------|
| 修复SIGSEGV | 末尾SIGSEGV报告为"既有内核IPC问题"，定位并修复（可能与ucontext栈/僵尸回收时序相关） |
| 新增测试文件 | `test/test_kernel/test_sched.cpp`, `test/test_kernel/test_vfs.cpp`, `test/test_kernel/test_sysfs.cpp` |
| Mock框架 | 为HAL层创建测试mock（hal_display, hal_input），使内核逻辑可独立测试 |
| CI目标 | 所有内核模块 ≥90% 行覆盖率 |

**验证**: `pio test -e native` 100%通过，无SIGSEGV。

---

### Phase 2: 抢占式调度器

**目标**: 将协作式Round-Robin升级为基于优先级的抢占式调度器，但保持向后兼容（通过配置切换）。

#### Module 2.1: 可插拔调度器接口

**新文件**: `src/kernel/kern_sched_class.h`

```c
// 调度类接口（类似Linux sched_class）
typedef struct kern_sched_class {
    const char *name;
    
    // 任务入队/出队
    void (*enqueue)(kern_task_t *task);
    void (*dequeue)(kern_task_t *task);
    
    // 选择下一个运行的任务
    kern_task_t *(*pick_next)(void);
    
    // 时钟tick通知（抢占判断）
    void (*tick)(void);
    
    // 优先级变更
    void (*prio_changed)(kern_task_t *task, uint8_t old_prio);
    
    // 所属任务链表头
    kern_task_t *task_list;
} kern_sched_class_t;
```

**多个调度类按优先级排序**（高优先级类先选任务）:
1. `sched_class_edf` — 最早截止时间优先（实时任务）
2. `sched_class_fifo` — 固定优先级抢占（交互任务）
3. `sched_class_rr` — 时间片轮转（普通任务，向后兼容当前行为）

**全局调度器**（`kern_sched.c`）:
```c
// 按优先级排序的调度类数组
extern kern_sched_class_t *g_sched_classes[];
extern int g_num_sched_classes;

// 新的 pick_next 遍历所有调度类
kern_task_t *kern_sched_pick_next(void) {
    for (int i = 0; i < g_num_sched_classes; i++) {
        kern_task_t *t = g_sched_classes[i]->pick_next();
        if (t) return t;
    }
    return g_idle_task;
}
```

**向后兼容**: 默认只注册 `sched_class_rr`，行为与当前完全相同。

**新文件**: `src/kernel/kern_sched_rr.c`, `src/kernel/kern_sched_fifo.c`, `src/kernel/kern_sched_edf.c`

#### Module 2.2: 定时器中断驱动

**目标**: 利用ESP32硬件定时器实现周期性tick中断，驱动抢占。

**方案**:
- 使用ESP32 Timer Group 0, Timer 0 作为系统tick源
- 配置为1ms周期（可配置 `CONFIG_TICK_RATE_HZ`）
- ISR中调用 `kern_sched_tick()` 检查抢占
- 通过 `kern_port` 接口实现上下文切换（当前：信号量通知 + FreeRTOS任务切换）

**抢占检查逻辑** (`sched_class_fifo->tick()`):
```
tick():
    if current->timeslice > 0:
        current->timeslice--
    if higher_priority_task_ready():
        set_need_resched()  // 标记抢占标志
```

**PlatformIO配置**: `build_flags += -DCONFIG_PREEMPT_ENABLED`

#### Module 2.3: 优先级调度类实现

**文件**: `src/kernel/kern_sched_fifo.c`

- 就绪队列按优先级分组（位图 + 链表，O(1)查找最高优先级）
- 同一优先级内FIFO（先入先出）
- 时间片用完后降级到RR类

**文件**: `src/kernel/kern_sched_rr.c`

- 从当前fifo.c提取Round-Robin逻辑
- 保留 `g_last_picked` 公平性
- 作为默认调度类（最低优先级兜底）

**文件**: `src/kernel/kern_sched_edf.c`

- 红黑树按 `deadline` 排序
- 任务创建时指定 `period` 和 `deadline`
- 可选编译（`CONFIG_SCHED_EDF`）

**验证**:
- `pio test -e native` 调度类测试
- 抢占压力测试：高优先级任务持续就绪，验证低优先级被抢占
- 配置切换测试：`CONFIG_PREEMPT_ENABLED=0` 时行为与当前一致

---

### Phase 3: SMP 多核支持

**目标**: 利用ESP32双核（PRO_CPU + APP_CPU）并行执行任务。

#### Module 3.1: Per-CPU 数据结构

**方案**: 将当前全局单例改造为per-CPU数组。

| 变量 | 改造为 | 
|------|--------|
| `g_current_task` | `g_per_cpu[core].current_task` |
| `g_idle_task` | `g_per_cpu[core].idle_task` |
| `g_last_picked` | 移到调度类内部（per-cpu runqueue） |
| `g_task_list` | 全局共享 + per-cpu runqueue |

**新文件**: `src/kernel/kern_smp.h`, `src/kernel/kern_smp.c`

```c
#define KERN_MAX_CPUS 2  // ESP32

typedef struct kern_per_cpu {
    uint8_t         cpu_id;
    kern_task_t    *current_task;
    kern_task_t    *idle_task;
    uint32_t        sched_ticks;
    bool            need_resched;
    // ... 其他per-cpu字段
} kern_per_cpu_t;

extern kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];

// 获取当前CPU ID（ESP32: xPortGetCoreID()）
static inline uint8_t kern_cpu_id(void);
```

#### Module 3.2: 跨核任务迁移

**设计决策**: 任务创建时绑定到指定CPU（`cpu_affinity`），不自动迁移以避免缓存失效。可选 `KERN_CPU_ANY` 表示由负载均衡器选择。

**负载均衡**:
- 简单策略：新任务分配到任务数最少的CPU
- 工作窃取：空闲CPU从最忙CPU的runqueue尾部窃取任务
- 周期性均衡：每100ms检查CPU负载差异

**新字段** (kern_task_t):
```c
uint8_t   cpu_affinity;   // 绑定的CPU，KERN_CPU_ANY=不绑定
uint8_t   cpu_current;    // 当前所在CPU
```

#### Module 3.3: SMP 同步原语

**需要新增的同步原语**（因为SMP使竞态条件成为现实）:

| 原语 | 实现 | 用途 |
|------|------|------|
| `spinlock_t` | 硬件CAS自旋锁 | 保护per-CPU runqueue、调度器状态 |
| `mutex_t` | 基于spinlock + 阻塞队列 | 任务间互斥 |
| `semaphore_t` | 基于spinlock + 等待队列 | 任务间同步 |

**关键**: 这些同步原语只在与FreeRTOS后端绑定时使用FreeRTOS的信号量；在裸机后端时使用自旋锁+自旋等待。

**验证**:
- 双核并行测试：两个CPU同时运行独立任务
- SMP竞态测试：多任务并发访问共享数据
- `pio run -e m5stick-c` 编译通过（核心测试需要硬件）

---

### Phase 4: MPU 内存保护

**目标**: 利用ESP32 MPU隔离任务内存空间，捕获非法访问并优雅处理。

#### Module 4.1: MPU 区域配置

**ESP32 MPU特性**:
- 每个CPU有独立的MPU（PRO_CPU MPU + APP_CPU MPU）
- 最多8个区域
- 区域大小必须是2的幂，对齐到大小
- 权限：R/W/X 可分别控制

**区域分配方案**:
```
区域0: 内核代码段 (.text)      — 只读，用户不可访问
区域1: 内核数据段 (.data/.bss) — 读写，用户不可访问  
区域2: 外设寄存器区             — 读写，用户不可访问
区域3: 当前任务栈              — 读写，仅当前任务可访问（上下文切换时重配置）
区域4: 当前任务堆              — 读写，仅当前任务可访问（上下文切换时重配置）
区域5-7: 保留
```

**新文件**: `src/kernel/kern_mpu.h`, `src/kernel/kern_mpu.c`

```c
int kern_mpu_init(void);                         // 配置内核保护区
int kern_mpu_switch_task(kern_task_t *task);     // 切换任务时重配MPU
int kern_mpu_protect_region(void *base, size_t size, uint8_t permissions);
```

**实现**: 通过 `kern_port_ops.mpu_configure` 调用底层ESP32 MPU寄存器操作（`xtensa` 的 `WPTLBE` / `xthal_set_region_attribute`）。

#### Module 4.2: MemManage 异常处理

**ESP32异常向量**:
- `ExceptionCause=4` (Level1Interrupt) — MPU访问违规
- `ExceptionCause=28` (LoadProhibitedCause) — 加载保护违规
- `ExceptionCause=29` (StoreProhibitedCause) — 存储保护违规

**异常处理流程**:
```
MPU异常 → 保存异常帧 → 识别违规任务 → 标记任务为FAULTED 
→ 记录诊断信息(PC, 违规地址, 访问类型) → 
   ├── 可恢复? → 跳过违规指令，继续执行 (如读NULL)
   └── 不可恢复? → kern_task_terminate(task) + 通知监控任务
```

**新文件**: `src/kernel/kern_exc.c`, `src/kernel/kern_exc_xtensa.S` (汇编异常入口)

#### Module 4.3: 任务优雅重启

**流程**:
```
task_crash_detected(task):
    1. 遍历 TCB 资源列表 → 释放所有持有的锁 + 内存
    2. 关闭所有打开的文件描述符
    3. 通知 /proc/faults (新增procfs文件，记录崩溃历史)
    4. 调用 task->on_crash 回调（如重新创建UI任务）
    5. 将 TCB 标记为 ZOMBIE → reap_zombies 回收
```

**新增字段** (kern_task_t):
```c
void (*on_crash)(kern_task_t *task);  // 崩溃回调，由创建者设置
uint32_t crash_count;                  // 崩溃计数（超过阈值不再重启）
```

**验证**:
- 注入野指针写入，验证MPU捕获并重启任务
- 验证系统在任务崩溃后继续正常运行
- `pio test -e native` 模拟MPU异常处理逻辑（硬件MPU仅在ESP32上测试）

---

### Phase 5: 资源追踪与安全终止

#### Module 5.1: TCB 资源列表

**新字段** (kern_task_t):
```c
// 资源链表节点
typedef struct kern_resource {
    void                  *ptr;        // 资源指针
    kern_resource_type_t   type;       // MEMORY / MUTEX / SEMAPHORE / FD / FILE
    void                 (*release)(void *ptr);  // 释放回调
    struct kern_resource  *next;
} kern_resource_t;

// TCB中新增
kern_resource_t *resource_head;  // 资源链表头
```

**API**:
```c
int kern_resource_track(kern_task_t *task, void *ptr, 
                        kern_resource_type_t type, void (*release)(void *));
int kern_resource_untrack(kern_task_t *task, void *ptr);
void kern_resource_release_all(kern_task_t *task);  // 释放所有资源
```

**自动化**: 在分配mutex/semaphore/memory时自动调用 `kern_resource_track()`。

#### Module 5.2: 统一内存分配器

**新文件**: `src/kernel/kern_kmalloc.h`, `src/kernel/kern_kmalloc.c`

```c
// 内核内存分配器（替代直接 malloc/free）
void *kern_kmalloc(size_t size, kern_task_t *owner);
void *kern_kcalloc(size_t nmemb, size_t size, kern_task_t *owner);
void  kern_kfree(void *ptr);
void *kern_krealloc(void *ptr, size_t new_size);

#define kmalloc(sz)    kern_kmalloc(sz, kern_task_current())
#define kfree(ptr)     kern_kfree(ptr)
```

**追溯性**: `kern_kmalloc` 在分配的内存块头部存储owner指针，用于：
1. 自动在 `kern_resource_track` 注册
2. 任务终止时自动释放所有内存
3. 检测double-free / use-after-free（debug模式）

**支持内存池**: 对小对象（≤256字节）使用slab分配器，对大对象回退到 `malloc`。

#### Module 5.3: 安全终止流程

**新的 kern_exit 流程**:
```
kern_exit():
    1. 关闭所有打开的文件描述符 (fd_table 遍历 → kern_close)
    2. kern_resource_release_all(current) — 释放所有追踪的资源
    3. 如果可能，通知等待此任务的其他任务
    4. 设置状态为 ZOMBIE
    5. 上下文切换到调度器
```

**新的 kern_task_kill 流程** (外部终止):
```
kern_task_kill(pid):
    1. 验证权限（不能杀保护任务）
    2. 获取目标任务的spinlock
    3. 设置 kill_requested 标志
    4. 如果目标在另一个CPU上，发送IPI强制进入内核
    5. 目标任务下次进入内核时检测标志 → 调用 kern_exit()
```

**验证**:
- 任务持有mutex后被杀 → 验证mutex被自动释放
- 任务分配内存后被杀 → 验证内存被回收
- `pio test -e native` 内存泄漏检测（valgrind）

---

### Phase 6: 统一设备驱动模型

#### Module 6.1: struct device + driver 框架

**新文件**: `src/kernel/kern_device.h`, `src/kernel/kern_device.c`, `src/kernel/kern_driver.h`

```c
// 统一设备模型
typedef struct kern_device {
    const char          *name;        // 设备名 (如 "fb0")
    const char          *devpath;     // /dev/ 路径
    kern_device_type_t   type;        // CHAR / BLOCK / NET / BUS
    
    // 操作表
    struct kern_device_ops {
        int  (*open)(struct kern_device *dev, int flags);
        int  (*close)(struct kern_device *dev);
        int  (*read)(struct kern_device *dev, void *buf, size_t len, size_t *offset);
        int  (*write)(struct kern_device *dev, const void *buf, size_t len, size_t *offset);
        int  (*ioctl)(struct kern_device *dev, unsigned int cmd, unsigned long arg);
        int  (*suspend)(struct kern_device *dev);
        int  (*resume)(struct kern_device *dev);
    } *ops;
    
    // 驱动绑定
    struct kern_driver   *driver;     // 绑定的驱动
    void                 *driver_data; // 驱动私有数据
    
    // 中断（Module 6.2）
    int                   irq;
    void                (*irq_handler)(struct kern_device *dev);
    
    // 电源管理
    kern_device_power_state_t power_state;
    
    // 设备树链表
    struct kern_device   *parent;
    struct kern_device   *children[8];
    uint8_t               child_count;
    struct kern_device   *next;       // 全局设备链表
} kern_device_t;

// 驱动结构
typedef struct kern_driver {
    const char          *name;
    kern_device_type_t   supported_types;
    int  (*probe)(struct kern_device *dev);
    void (*remove)(struct kern_device *dev);
    struct kern_driver  *next;
} kern_driver_t;

// 设备注册API
int kern_device_register(kern_device_t *dev);
int kern_device_unregister(kern_device_t *dev);
int kern_driver_register(kern_driver_t *drv);
int kern_device_attach(kern_device_t *dev, kern_driver_t *drv);
```

**与VFS的关系**: 设备注册后自动通过 `kern_devfs` 暴露到 `/dev/<name>`。`kern_open("/dev/fb0")` 内部路径解析到设备的dentry后，VFS操作委托给 `device->ops`。

#### Module 6.2: 中断管理框架

**新文件**: `src/kernel/kern_irq.h`, `src/kernel/kern_irq.c`

```c
// 中断请求
typedef int kern_irq_t;

// 中断处理函数
typedef void (*kern_irq_handler_t)(kern_irq_t irq, void *dev);

// API
int kern_irq_request(kern_irq_t irq, kern_irq_handler_t handler, 
                     uint32_t flags, const char *name, void *dev);
void kern_irq_free(kern_irq_t irq, void *dev);

// 下半部机制
int kern_irq_schedule_work(kern_irq_t irq);  // 调度下半部处理
```

**实现策略**:
- **上半部** (ISR): 最小化，仅读取硬件寄存器、清除中断标志、写入环形缓冲区
- **下半部** (tasklet/workqueue): 通过消息队列将数据传递给等待该中断的内核任务
- **与Phase 2定时器集成**: 定时器中断通过同一框架注册

**ESP32中断映射**: 利用 `esp_intr_alloc()` 分配中断，在 `kern_port_freertos.c` 中桥接。

#### Module 6.3: 现有设备迁移

| 当前设备 | 迁移后 |
|----------|--------|
| `/dev/fb0` (dev_fb0.c) | `kern_device_t fb0_dev` + `fb0_driver`，实现 `kern_device_ops` |
| `/dev/input0` (dev_input0.c) | `kern_device_t input0_dev` + `input0_driver` |
| `/dev/ttyS0` (dev_ttyS0.cpp) | `kern_device_t ttyS0_dev` + `ttyS0_driver` |
| `/dev/pwrkey` (dev_pwrkey.c) | `kern_device_t pwrkey_dev` + `pwrkey_driver` |

**迁移原则**: 每个设备的 `kern_file_ops_t` 作为 `kern_device_ops` 的底层实现保留，添加 `open/close/suspend/resume` 和驱动绑定逻辑。现有 shell/应用代码完全不变。

**迁移后清理**:
- 移除 `dev_fb0.h/c` 中的 VFS 注册代码（由统一设备框架处理）
- 移除 `dev_input0.h/c` 中的手动 VFS 注册
- 移除 `dev_ttyS0.cpp` 中的手动 VFS 注册
- 移除 `dev_pwrkey.c` 中的手动 VFS 注册
- 简化 `kern_devices.c` → 仅保留设备实例创建 + 注册调用

---

## 第三部分：依赖关系与执行序列

```
Phase 1: 基础重构
    │
    ├── Phase 2: 抢占式调度器 (依赖 Phase 1)
    │       │
    │       ├── Phase 3: SMP 支持 (依赖 Phase 2 可插拔接口)
    │       │       │
    │       │       └── Phase 4: MPU 内存保护 (可与 Phase 3 并行)
    │       │
    │       └── Phase 5: 资源追踪 (依赖 Phase 2 抢占，可与 Phase 3/4 部分并行)
    │
    └── Phase 6: 统一设备模型 (仅依赖 Phase 1，可随时开始)
```

### 推荐的执行顺序

1. **Phase 1** (1-2天) — 必须最先完成
2. **Phase 2** (2-3天) — 核心变更，解锁后续
3. **Phase 5** (2-3天) — 可与Phase 2部分重叠（资源追踪的TCB字段在Phase 3/4中需要）
4. **Phase 3** (3-4天) — 依赖Phase 2的可插拔接口 + Phase 5的同步原语
5. **Phase 4** (2-3天) — 可与Phase 3并行或在其后
6. **Phase 6** (2-3天) — 独立，可在任何Phase 1之后开始

### 风险与缓解

| 风险 | 严重度 | 缓解措施 |
|------|--------|----------|
| FreeRTOS与抢占式调度器冲突 | 🔴 | Phase 2保留配置开关 `CONFIG_PREEMPT_ENABLED`，默认关闭。逐步测试后开启。 |
| SMP引入Heisenbug | 🔴 | 严格的测试用例 + ESP32硬件验证。`spinlock` 实现需审计。 |
| MPU配置错误导致内核崩溃 | 🟡 | MPU错误配置会导致立即HardFault，调试困难。先在QEMU/模拟器上测试MPU配置逻辑。 |
| 现有186个native测试全部失败 | 🟡 | Phase 1完成后全部通过再进入Phase 2。每Phase完成后运行全量测试。 |
| 内存/Flash占用增长超限 | 🟡 | ESP32-PICO flash 4MB, RAM 520KB。当前使用Flash 91.2%, RAM 21.2%。每个Phase完成后监测增长。 |
| 抢占式调度破坏协作式假设 | 🔴 | 现有App层代码假设无抢占（无锁）。需要用 `mutex_t` 保护所有共享状态，逐步审计App层。 |

---

## 第四部分：验证策略

### 测试层级

| 层级 | 环境 | 覆盖内容 |
|------|------|----------|
| L0: 单元测试 | PlatformIO Native + GoogleTest | 每个新模块的独立函数 |
| L1: 集成测试 | PlatformIO Native | 模块间交互（调度器+VFS、设备+中断） |
| L2: 硬件编译 | `pio run -e m5stick-c` | 确保ESP32编译通过、Flash/RAM不超限 |
| L3: 硬件功能测试 | M5Stick-C 实机 | MPU异常、SMP并行、抢占行为（人工） |

### 每Phase完成标准

1. `pio test -e native` 全部通过，无回归
2. `pio run -e m5stick-c` 编译成功
3. 新增代码有对应测试覆盖（≥80%行覆盖率）
4. `.claude/prompt/feat.md` 状态更新

---

## 第五部分：文件变更预估

| Phase | 新增文件 | 修改文件 | 删除文件 | 净增代码行 |
|-------|----------|----------|----------|-----------|
| Phase 1 | 6 | 5 | 0 | ~400 |
| Phase 2 | 6 | 3 | 0 | ~800 |
| Phase 3 | 3 | 4 | 0 | ~600 |
| Phase 4 | 4 | 2 | 0 | ~500 |
| Phase 5 | 3 | 2 | 0 | ~400 |
| Phase 6 | 4 | 7 | 0 | ~600 |
| **总计** | **26** | **23** | **0** | **~3300** |

现有内核约5000行代码，净增约3300行，总内核代码量约8300行。

---

*本计划由 feat-workflow 生成，提交人工审查后逐 Phase 执行。*
