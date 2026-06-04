# MPU 内存保护（Kern MPU）

> **Parent:** [内核总览](index.md) | **Related:** [调度器](kern-task.md), [SMP 多核](kern-smp.md), [资源追踪](kern-resource.md), [类型系统](kern-types.md)

## 概述

`kern_mpu` 利用 ESP32 的 Memory Protection Unit（PMS: Processor Memory Subsystem）为每个任务提供**硬件级地址空间隔离**。每个 CPU 核拥有 8 个独立 MPU 区域，可以为任务配置「可读」「可写」「可执行」「禁止访问」等权限。该模块的核心价值是：

- **栈溢出检测**：在任务栈底部设置不可访问的守卫页，栈越界时硬件立即触发异常而非静默覆盖相邻数据
- **任务间隔离**：防止一个任务误写其他任务的数据段
- **内核保护**：将内核代码和数据标记为特权级只读，用户任务无法篡改

当 `CONFIG_MPU_ENABLED` 未定义时，所有 API 退化为零开销的空操作宏——无代码膨胀，无性能损失，完全向后兼容。

---

## 关键概念

### MPU 区域类型

*📄 Source: [kern_mpu.h](../../src/kernel/kern_mpu.h#L37-L51)*

```c
typedef enum {
    KERN_MPU_ACCESS_NONE  = 0,   /* 禁止访问 */
    KERN_MPU_ACCESS_RO    = 1,   /* 只读 */
    KERN_MPU_ACCESS_RW    = 2,   /* 读写 */
    KERN_MPU_ACCESS_RX    = 3,   /* 读执行 */
    KERN_MPU_ACCESS_RWX   = 4,   /* 读写执行 */
} kern_mpu_access_t;

typedef enum {
    KERN_MPU_TYPE_UNUSED      = 0,  /* 未使用 */
    KERN_MPU_TYPE_KERNEL      = 1,  /* 内核代码/数据（特权级） */
    KERN_MPU_TYPE_STACK_GUARD = 2,  /* 栈守卫页（溢出检测） */
    KERN_MPU_TYPE_TASK_DATA   = 3,  /* 任务数据段（隔离） */
    KERN_MPU_TYPE_PERIPHERAL  = 4,  /* 外设映射 */
} kern_mpu_region_type_t;
```

### MPU 区域描述符

*📄 Source: [kern_mpu.h](../../src/kernel/kern_mpu.h#L55-L61)*

```c
typedef struct kern_mpu_region {
    void                 *base;       /* 区域起始地址（须按 32 字节对齐） */
    size_t                size;       /* 区域大小（字节） */
    kern_mpu_access_t     access;     /* 访问权限 */
    kern_mpu_region_type_t type;     /* 区域类型 */
    bool                  enabled;    /* 是否启用 */
} kern_mpu_region_t;
```

### 每任务 MPU 配置

*📄 Source: [kern_mpu.h](../../src/kernel/kern_mpu.h#L71-L74)*

```c
typedef struct kern_mpu_config {
    uint8_t            region_count;                /* 已配置的区域数 */
    kern_mpu_region_t  regions[KERN_MPU_MAX_REGIONS];  /* 区域配置数组 */
} kern_mpu_config_t;
```

每个 TCB 的 `mpu_config` 字段指向一个 `kern_mpu_config_t`，在上下文切换时由 `kern_mpu_apply()` 写入 CPU 硬件寄存器。

---

## ESP32 MPU 寄存器编码

ESP32 的 PMS（Processor Memory Subsystem）为每个核提供 8 个区域寄存器 `PMS_C0` ~ `PMS_C7`，每个是一个 32 位寄存器：

```
 31:28     27:25      24:21        20:14            13:0
┌──────┬───────────┬──────────┬──────────────┬──────────────┐
│ 保留  │ 访问权限  │   保留   │  区域大小编码  │  区域基址    │
│      │0=None     │          │  log2(size)-1  │  [27:14]     │
│      │1=R        │          │  0=禁用        │  32字节对齐  │
│      │2=RW       │          │                │              │
│      │3=RX       │          │                │              │
│      │4=RWX      │          │                │              │
└──────┴───────────┴──────────┴──────────────┴──────────────┘
```

### mpu_encode_size — 区域大小编码算法

*📄 Source: [kern_mpu.c](../../src/kernel/kern_mpu.c#L105-L130)*

```c
static uint32_t mpu_encode_size(size_t size)
{
    if (size < KERN_MPU_MIN_ALIGN) return 0;  /* 小于 32 字节 → 禁用 */

    /* 向上取整到 2 的幂 */
    uint32_t power = 32;  /* 最小 32 字节 = 2^5 */
    uint32_t aligned = KERN_MPU_MIN_ALIGN;

    while (aligned < size && power < 0xFFFFFFFF) {
        aligned <<= 1;
        power <<= 1;
    }

    if (aligned >= KERN_MPU_MIN_ALIGN) {
        uint32_t encoded = 0;
        uint32_t s = aligned;
        while (s > KERN_MPU_MIN_ALIGN) {
            s >>= 1;
            encoded++;
        }
        return encoded + 1;  /* 32 字节 = 编码 1 */
    }
    return 0;
}
```

#### 中文伪代码拆解

```
函数 编码区域大小(字节大小) {
    if (字节大小 < 32字节) return 0   // ESP32要求最小32字节

    /* 步骤一：向上取整到最接近的 2^n */
    对齐后大小 = 32
    while (对齐后大小 < 字节大小) {
        对齐后大小 = 对齐后大小 × 2   // 翻倍直到能覆盖
    }

    /* 步骤二：计算编码值 */
    /* 编码规则：log2(对齐后大小) - 4 */
    /*   32字节 → 编码1, 64字节 → 编码2, 128字节 → 编码3 ... */
    编码值 = 0
    临时值 = 对齐后大小
    while (临时值 > 32) {
        临时值 = 临时值 / 2
        编码值 = 编码值 + 1
    }

    return 编码值 + 1    // 补偿初始偏移
}
```

**编码速查表**：

| 实际大小 | 编码值 | 覆盖范围说明 |
|---------|-------|-------------|
| 32 B | 1 | 最小保护单元（守卫页） |
| 64 B | 2 | |
| 128 B | 3 | |
| 256 B | 4 | |
| 512 B | 5 | |
| 1 KB | 6 | 典型小任务栈守卫 |
| 2 KB | 7 | |
| 4 KB | 8 | 默认栈大小守卫 |
| 8 KB | 9 | 大栈守卫 |

---

## 栈守卫机制

### 工作原理

*📄 Source: [kern_mpu.c](../../src/kernel/kern_mpu.c#L134-L158)*

```
             栈高地址（stack_base + stack_size）
             ↓
    ┌─────────────────────┐  ← SP 初始位置（栈顶）
    │                     │
    │   可用栈空间         │  ← 正常读写
    │   (可读写)           │
    │                     │
    │                     │
    ├─────────────────────┤  ← 栈守卫边界
    │   栈守卫区域         │  ← 禁止访问 (KERN_MPU_ACCESS_NONE)
    │   (32 字节)          │     栈溢出触发 MPU 异常
    └─────────────────────┘  ← stack_base（栈底）
             栈低地址
```

任务在正常使用栈时，SP 在可用栈空间内移动。一旦发生栈溢出（如递归过深或局部变量过大），SP 会越过边界进入栈守卫区域。此时任何读写操作都会触发 ESP32 的 `IllegalAccess` 异常，立即将问题暴露出来——而非静默覆盖相邻任务的数据。

#### 中文伪代码拆解

```
函数 设置栈守卫(任务, 栈基址, 栈大小) {
    if (任务为空 或 栈基址为空 或 栈大小=0) return

    配置 = 任务.MPU配置
    if (配置为空) return

    守卫大小 = 守卫页数 × 32字节       // 默认 1 页 = 32 字节
    if (守卫大小 > 栈大小)
        守卫大小 = 栈大小               // 栈太小则整个栈作为守卫

    守卫基址 = 向下对齐到32字节边界(栈基址)

    if (配置.区域数 < 最大区域数) {
        索引 = 配置.区域数
        配置.区域[索引] = {
            .基址 = 守卫基址,
            .大小 = 守卫大小,
            .权限 = 禁止访问,          // ← 关键：栈底部设为不可访问
            .类型 = 栈守卫,
            .启用 = true
        }
        配置.区域数++
    }
}
```

---

## ESP32 8 个 MPU 区域规划

*📄 Source: [kern_mpu.c](../../src/kernel/kern_mpu.c#L26-L43)*

```
区域索引    类型                 权限         用途
─────────────────────────────────────────────────────
C0          KERNEL              只读/执行     内核 .text 段保护
C1          KERNEL              只读         内核 .rodata 段保护
C2          STACK_GUARD         禁止访问      当前任务栈溢出检测
C3          STACK_GUARD         禁止访问      栈守卫第二页（可选）
C4          TASK_DATA           读写          当前任务数据段隔离
C5          TASK_DATA           读写          当前任务堆/BSS 段
C6          PERIPHERAL          读写          外设寄存器映射
C7          UNUSED              —            预留扩展
```

C0-C1 在所有任务间**共享**（内核区域不变），在 `kern_mpu_init()` 时配置。C2-C5 是**每任务独立**的，在上下文切换时由 `kern_mpu_apply()` 动态写入。

---

## 上下文切换时的 MPU 应用

*📄 Source: [kern_mpu.c](../../src/kernel/kern_mpu.c#L195-L224)*

```c
void kern_mpu_apply(struct kern_task *task)
{
    if (task == NULL) return;
    kern_mpu_config_t *cfg = task->mpu_config;
    if (cfg == NULL) return;

    /* 逐区域写入 MPU 寄存器 */
    for (uint8_t i = 0; i < cfg->region_count; i++) {
        kern_mpu_region_t *rgn = &cfg->regions[i];
        if (!rgn->enabled) continue;

        uint32_t reg_val =
            ((uint32_t)(uintptr_t)rgn->base & 0x3FFF)           /* [13:0] 基址 */
          | (mpu_encode_size(rgn->size) << 14)                   /* [20:14] 大小 */
          | (mpu_encode_access(rgn->access) << 25);              /* [27:25] 权限 */

        /* 写入对应核的寄存器 */
        switch (kern_cpu_id()) {
            case 0: DPORT_PRO_PMS_C(i) = reg_val; break;
            case 1: DPORT_APP_PMS_C(i) = reg_val; break;
        }
    }
    asm volatile("memw");  /* 刷新 MPU 状态 */
}
```

#### 中文伪代码拆解

```
函数 应用MPU配置(目标任务) {
    if (任务为空) return
    配置 = 任务.MPU配置
    if (配置为空) return    // 此任务未配置 MPU

    遍历 i = 0 到 配置.区域数-1 {
        区域 = 配置.区域数组[i]
        if (区域.未启用) continue

        /* 组装 32 位寄存器值 */
        寄存器值 = (区域基址的低14位)
                 | (编码区域大小(区域.大小) << 14)
                 | (编码权限位(区域.权限) << 25)

        /* 根据当前 CPU 核写入对应寄存器组 */
        if (当前核 == 0)
            PRO核区域寄存器[i] = 寄存器值
        else if (当前核 == 1)
            APP核区域寄存器[i] = 寄存器值
    }

    内存屏障()   // 确保寄存器写入对所有总线访问者可见
}
```

---

## CONFIG_MPU_ENABLED 零开销退化

*📄 Source: [kern_mpu.h](../../src/kernel/kern_mpu.h#L117-L131)*

当 `CONFIG_MPU_ENABLED` 未定义时（如 `native` 测试环境或资源受限场景），所有 MPU API 退化为零开销空操作：

```c
#define kern_mpu_init()                    do {} while (0)
#define kern_mpu_apply(t)                  do { (void)(t); } while (0)
#define kern_mpu_setup_stack_guard(t,b,s)  do { (void)(t); (void)(b); (void)(s); } while (0)

static inline int kern_mpu_add_region(struct kern_task *task,
                                      void *base, size_t size,
                                      kern_mpu_access_t access)
{
    (void)task; (void)base; (void)size; (void)access;
    return KERN_OK;  /* 始终返回成功 */
}
```

| 特性 | CONFIG_MPU_ENABLED 启用 | 未启用（退化） |
|------|----------------------|-------------|
| 代码体积 | +~300 字节 | 0 字节 |
| 运行时开销 | 每上下文切换约 8-16 次寄存器写入 | 无 |
| 栈溢出检测 | 硬件触发 IllegalAccess 异常 | 金丝雀检测（软件，延迟发现） |
| 任务间隔离 | 硬件强制 | 依赖程序员自律 |
| 编译选项 | `-D CONFIG_MPU_ENABLED` | 默认 |

**为什么要有退化模式？**
1. **Native 测试环境**：x86_64 没有 ESP32 的 PMS 寄存器，MPU 操作无意义
2. **逐模块开启**：SMP 和抢占式调度足够复杂时，可先关闭 MPU 降低调试难度，调通后再开启
3. **Flash 空间紧张**：当前 Flash 使用率 99.4%，关闭 MPU 可节省 ~300 字节代码

---

## 初始化与集成点

```
kern_sched_init()
    ├── kern_mpu_init()          ← 配置内核保护区（C0-C1）
    ├── kern_smp_init()          ← 初始化双核 per-CPU 数据
    └── 创建 idle 任务
            └── kern_spawn("idle", ...)
                    └── kern_mpu_setup_stack_guard()  ← 为 idle 设置栈守卫

kern_spawn("my_task", ...)
    ├── 创建 TCB + 分配栈
    ├── kern_mpu_setup_stack_guard()   ← 为新任务设置栈守卫
    └── 注册到调度器

上下文切换（kern_sched_tick → switch_to）:
    kern_mpu_apply(next_task)    ← 切换到新任务的 MPU 配置
```

---

> **See Also:** [调度器与任务管理](kern-task.md) | [SMP 多核支持](kern-smp.md) | [资源追踪](kern-resource.md) | [内核初始化](kern-init.md) | [可移植层](kern-port.md)
