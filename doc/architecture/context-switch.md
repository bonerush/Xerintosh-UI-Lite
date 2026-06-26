# 上下文切换设计

> **Parent:** [原生内核架构](xeros-native-kernel.md) | **Related:** [调度器](scheduler.md)

## 概述

上下文切换是原生内核最底层、最关键的组件。本文档描述使用 Xtensa call0 ABI 实现上下文切换的完整设计，替代当前已知有 bug 的 setjmp/longjmp 方案。

## Xtensa 架构背景

### 寄存器窗口机制

ESP32 使用 Xtensa LX6 核心，具有：
- **64 个物理通用寄存器** (ar0-ar63)
- **16 个可见寄存器** (a0-a15) 通过滑动窗口访问
- **WindowBase** (特殊寄存器 #72) 标识当前窗口位置
- **WindowStart** (特殊寄存器 #73) 位掩码标识有效窗口

```
物理寄存器布局 (64 个):
┌────┬────┬────┬────┬────┬────┬────┬────┐
│ ar0│ ar1│... │ar15│ar16│... │ar47│... │ar63│
└────┴────┴────┴────┴────┴────┴────┴────┘
         ↑
    WindowBase 指向当前窗口起始位置
    每次 CALL8: WindowBase += 2
```

### 窗口溢出/下溢异常

当访问的寄存器属于已溢出的旧窗口时：
- **WindowOverflow**: 将被驱逐的窗口保存到栈上的 BSA/ESA 区域
- **WindowUnderflow**: 从栈恢复之前溢出的寄存器值

### setjmp/longjmp 失败原因

*📄 Source: [kern_ctx_esp32.h](../../src/kernel/kern_ctx_esp32.h)*

当前 setjmp/longjmp 方案在 ESP32 上失败的原因：
1. **SP 相对保存区损坏**: Xtensa newlib 的 setjmp 从当前 SP 读取 4 个字的保存区。在任务栈上调用 setjmp（SP 已切换后）读取到未初始化的 0xAA，导致 longjmp 恢复时 StoreProhibited 崩溃
2. **窗口状态不一致**: setjmp/longjmp 无法正确处理跨独立执行上下文的寄存器窗口状态（不同 WindowBase 值）
3. **共享栈冲突**: 共享栈方案中，init 代码返回后栈帧被复用，longjmp 恢复后与调度器帧冲突

## call0 ABI 方案

### 为什么选择 call0

| 特性 | 窗口 ABI | call0 ABI |
|------|----------|-----------|
| 寄存器数量 | 64 个物理，16 个可见 | 16 个固定 |
| 调用约定 | CALL4/8/12 旋转窗口 | CALL0 无旋转 |
| 栈切换安全性 | 需要管理窗口状态 | 绝对地址，安全 |
| 上下文切换复杂度 | 高（需保存全部 64 个） | 低（保存 16 个 + 特殊寄存器） |
| 性能 | 函数调用快（硬件旋转） | 函数调用稍慢（无硬件优化） |
| ESP-IDF 兼容性 | 默认 ABI | 需要 `-mabi=call0` |

### call0 寄存器分配

```
a0  = 返回地址 (RA)
a1  = 栈指针 (SP)
a2  = 第一个参数 / 返回值
a3-a7 = 参数寄存器
a8-a11 = 临时寄存器 (caller-saved)
a12-a15 = 保存寄存器 (callee-saved)
```

### 上下文帧结构

```c
typedef struct {
    // 通用寄存器 (call0 callee-saved)
    uint32_t a0;           // 返回地址
    uint32_t a1;           // 栈指针
    uint32_t a2;           // 参数/返回值
    uint32_t a3, a4, a5, a6, a7;  // 参数寄存器
    uint32_t a8, a9, a10, a11;    // 临时寄存器
    uint32_t a12, a13, a14, a15;  // 保存寄存器

    // 特殊寄存器
    uint32_t sar;          // 移位量寄存器
    uint32_t lbeg;         // 零开销循环起始
    uint32_t lend;         // 零开销循环结束
    uint32_t lcount;       // 零开销循环计数
    uint32_t ps;           // 处理器状态
    uint32_t exccause;     // 异常原因
    uint32_t excvaddr;     // 异常地址

    // 程序计数器
    uint32_t pc;           // 恢复地址
} kern_ctx_native_t;

#define KERN_CTX_NATIVE_SIZE  (24 * sizeof(uint32_t))  // 96 字节
```

## 核心汇编函数

### xeros_ctx_save

```asm
# 保存当前上下文到 ctx 指针 (a2)
# 返回 0 表示保存路径，1 表示恢复路径
.global xeros_ctx_save
.type xeros_ctx_save, @function
xeros_ctx_save:
    # 保存通用寄存器
    s32i a0,  a2, 0   # RA
    s32i a1,  a2, 4   # SP
    s32i a2,  a2, 8
    s32i a3,  a2, 12
    # ... a4-a15 ...
    s32i a15, a2, 60

    # 保存特殊寄存器
    rsr.sar    a3
    s32i a3, a2, 64
    rsr.lbeg   a3
    s32i a3, a2, 68
    rsr.lend   a3
    s32i a3, a2, 72
    rsr.lcount a3
    s32i a3, a2, 76
    rsr.ps     a3
    s32i a3, a2, 80

    # 保存返回地址作为 PC
    s32i a0, a2, 92

    # 返回 0 (保存路径)
    movi a2, 0
    ret

# 恢复路径入口 (从 xeros_ctx_restore 跳转)
xeros_ctx_save_restore_entry:
    movi a2, 1
    ret
```

### xeros_ctx_restore

```asm
# 从 ctx 指针 (a2) 恢复上下文
.global xeros_ctx_restore
.type xeros_ctx_restore, @function
xeros_ctx_restore:
    # 关闭中断
    rsil a3, XCHAL_EXCM_LEVEL

    # 恢复特殊寄存器
    l32i a3, a2, 64
    wsr.sar    a3
    l32i a3, a2, 68
    wsr.lbeg   a3
    l32i a3, a2, 72
    wsr.lend   a3
    l32i a3, a2, 76
    wsr.lcount a3
    l32i a3, a2, 80
    wsr.ps     a3

    # 恢复通用寄存器
    l32i a0,  a2, 0
    l32i a1,  a2, 4
    # ... a3-a15 ...
    l32i a15, a2, 60

    # 恢复 a2 最后
    l32i a2,  a2, 8

    # 跳转到保存的 PC
    jx a0
```

### xeros_flush_windows

```asm
# 刷新所有寄存器窗口到栈
# 在上下文切换前调用，确保所有窗口内容安全保存
.global xeros_flush_windows
.type xeros_flush_windows, @function
xeros_flush_windows:
    # 旋转窗口 4 次，每次旋转 4 个位置
    # 这会触发 WindowOverflow 异常，将脏窗口保存到栈
    rotw -4
    rotw -4
    rotw -4
    rotw -4
    ret
```

### xeros_ctx_init

```c
// ctx_init.c - 初始化新任务上下文
void xeros_ctx_init(kern_ctx_native_t *ctx, void *stack_base,
                     size_t stack_size, void (*entry)(void *), void *arg)
{
    memset(ctx, 0, KERN_CTX_NATIVE_SIZE);

    // 栈指针指向栈顶 (16 字节对齐)
    uint32_t sp = ((uint32_t)stack_base + stack_size) & ~0xF;
    ctx->a1 = sp;

    // 入口地址
    ctx->pc = (uint32_t)entry;
    ctx->a0 = (uint32_t)task_trampoline;  // 返回到退出蹦床

    // 第一个参数 (call0: a2 = 第一个参数)
    ctx->a2 = (uint32_t)arg;

    // 处理器状态: 用户模式 + 窗口关闭 + 中断级别
    ctx->ps = PS_UM | PS_CALLINC(0);  // call0 模式
}
```

## 调度器集成

### 调度器循环

```c
// kern_sched.c - XEROS_NATIVE_SCHED 路径
static kern_ctx_native_t g_sched_ctx;  // 调度器上下文

void kern_sched_tick(void)
{
    // ... reap_zombies, pick_next ...

    kern_task_t *next = pick_next_ready();
    if (next != NULL) {
        g_current_task = next;
        next->state = KERN_TASK_RUNNING;

        // 保存调度器上下文，恢复目标任务上下文
        if (xeros_ctx_save(&g_sched_ctx) == 0) {
            // 保存路径：切换到目标任务
            xeros_ctx_restore(next->native_ctx);
        }
        // 恢复路径：目标任务 yield/exit 后返回这里
    }
}
```

### 任务 yield

```c
// kern_task_lifecycle.c
void kern_yield(void)
{
    kern_task_t *task = g_current_task;
    task->state = KERN_TASK_READY;

    // 保存任务上下文，恢复调度器上下文
    if (xeros_ctx_save(task->native_ctx) == 0) {
        xeros_ctx_restore(&g_sched_ctx);
    }
    // 恢复路径：调度器再次选择此任务时返回这里
}
```

## 任务生命周期

```
kern_spawn()
    │
    ├── 分配栈 (kern_kmalloc)
    ├── 初始化上下文 (xeros_ctx_init)
    └── 加入就绪队列

任务执行:
    │
    ├── xeros_ctx_save(sched_ctx) → xeros_ctx_restore(task_ctx)
    │   (调度器 → 任务)
    │
    ├── 任务运行...
    │
    ├── kern_yield()
    │   ├── xeros_ctx_save(task_ctx) → xeros_ctx_restore(sched_ctx)
    │   └── (任务 → 调度器)
    │
    └── kern_exit()
        ├── 标记 ZOMBIE
        ├── 释放资源
        └── xeros_ctx_restore(sched_ctx)  // 不保存
```

## 验证策略

### 单元测试

```cpp
// test_ctx_switch.cpp
TEST(ContextSwitch, InitPopulatesAllFields) {
    kern_ctx_native_t ctx;
    uint8_t stack[4096];
    xeros_ctx_init(&ctx, stack, sizeof(stack), test_entry, (void*)0x1234);
    EXPECT_EQ(ctx.a1, ((uint32_t)stack + sizeof(stack)) & ~0xF);
    EXPECT_EQ(ctx.a2, 0x1234);
    EXPECT_NE(ctx.pc, 0);
}

TEST(ContextSwitch, SaveRestoreRoundTrip) {
    kern_ctx_native_t ctx;
    volatile int flag = 0;
    if (xeros_ctx_save(&ctx) == 0) {
        flag = 1;
        xeros_ctx_restore(&ctx);
    }
    EXPECT_EQ(flag, 1);
}
```

### 硬件测试

1. 创建两个测试任务，各自写入不同的栈模式
2. 在它们之间切换 100 次
3. 验证无寄存器损坏（检查 A0-A15 值）
4. 验证 PS.INTLEVEL 正确恢复
5. 栈金丝雀验证无溢出

---

> **See Also:** [原生内核架构](xeros-native-kernel.md) | [调度器](scheduler.md)
