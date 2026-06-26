# Watchdog Crash Debug Log

> **Parent:** [Index](index.md) | **Branch:** `debug/watchdog-crash` | **Start:** 2026-06-24

## 问题描述

系统烧录后崩溃，触发看门狗重启。需要定位崩溃点并修复。

## 调试环境

- **硬件:** M5Stick-P1 (ESP32-PICO)
- **内核:** Xeros v2.4.0 (自定义 RTOS，仿照 FreeRTOS API)
- **构建:** ESP-IDF + PlatformIO
- **调试工具:** `tools/xeros_debug.py` (串口调试)

---

## 调试会话记录

### 会话 1: 初始分析 (2026-06-24)

**思路:** 系统性分析代码架构，找出可能的崩溃点。

**已分析的文件:**
- `src/main.cpp` - 主入口
- `src/kernel/kern_port_freertos.c` - FreeRTOS 桥接层
- `src/kernel/esp32/ctx_switch.S` - 上下文切换汇编

**关键发现:**

#### 1. 双后端架构
项目有两种调度器后端：
- **FreeRTOS 后端** (默认): 使用 FreeRTOS 任务容器 + 双信号量令牌协议
- **原生调度器后端** (`XEROS_NATIVE_SCHED`): 使用自定义上下文切换

通过 `kern_port_ops_t` 结构体提供后端多态。

#### 2. FreeRTOS 后端分析
- 每个 Xeros 任务 = 1 个 FreeRTOS 任务
- 使用双二值信号量实现调度: `g_token_sem` (令牌) + `g_done_sem` (完成通知)
- 任务优先级: `tskIDLE_PRIORITY` (与 FreeRTOS idle 同级)
- 抢占通过 GPTimer ISR 实现，仅设置标志，调度在 loop() 中执行

**潜在问题点:**
- 任务超时 5 秒后标记为 ZOMBIE，可能导致资源泄漏
- 信号量操作可能死锁

#### 3. 原生调度器后端分析
- 使用 `xeros_ctx_save` / `xeros_ctx_restore` 实现上下文切换
- 汇编使用 call8 ABI (windowed calling convention)
- 新任务通过 trampoline 启动

**潜在问题点:**
- `xeros_ctx_init` 中 PS 值设置: `0x00040020` (WOE=1, UM=1)
- trampoline 使用 `callx8` 而非 `entry`，避免窗口溢出异常
- `xeros_ctx_restore` 中先恢复 PS 再 rsync，可能导致中断状态不一致

#### 4. main.cpp 启动流程
```
app_main()
  ├── hal_uart0_init()
  ├── nvs_flash_init()
  ├── storage_init()
  ├── hal_display_init()
  ├── hal_system_init()
  ├── hal_input_init()
  ├── boot_screen_show()
  ├── app_init_ui()
  ├── app_init_managers()
  ├── xerintosh_init_core()
  └── loop() {
        deferred_kernel_init()  // 第一帧执行
        dev_ttyS0_poll()
        serial_monitor_update()
        wifi_mgr_process_requests()
        kern_port_preempt_consume()
        kern_sched_tick()
        vTaskDelay(1)  // 让出 CPU 给 FreeRTOS idle
      }
```

**关键观察:**
- `vTaskDelay(1)` 在主循环中调用，让出 CPU 给 FreeRTOS idle 任务
- 这是防止中断看门狗 (INT_WDT, 300ms) 超时的关键
- 如果 Xeros 调度器任务优先级过高，会饿死 idle 任务

---

## 排除路径

### 已排除
- [ ] FreeRTOS 后端信号量初始化 (需要验证)
- [ ] 任务优先级设置 (需要验证)

### 待验证
- [ ] 实际崩溃日志 (需要收集)
- [ ] 当前使用哪个后端 (FreeRTOS 还是原生?)
- [ ] 调度器 tick 配置
- [ ] 中断优先级配置

### 可能的崩溃原因
1. **中断看门狗超时**: Xeros 任务饿死 FreeRTOS idle
2. **栈溢出**: 任务栈太小
3. **上下文切换错误**: 汇编实现有 bug
4. **信号量死锁**: 双信号量协议出错
5. **内存损坏**: 堆/栈越界

---

## 下一步行动

1. **收集崩溃日志**: 使用 `xeros_debug.py` 连接串口，复位设备，收集日志
2. **确认当前后端**: 检查构建配置，确认使用 FreeRTOS 还是原生调度器
3. **分析崩溃日志**: 根据日志定位具体崩溃点
4. **对比 FreeRTOS 实现**: 如果使用原生调度器，逐项对比 FreeRTOS 的 port 层

---

## 参考资料

- [ESP32 Xtensa ISA](https://0x04.net/~mwk/doc/xtensa.pdf)
- [FreeRTOS ESP32 Port](https://github.com/espressif/esp-idf/tree/master/components/freertos)
- [Xeros Architecture Doc](architecture/xeros-native-kernel.md)

---

## 深度分析

### 构建环境配置

项目有三个构建环境:

| 环境 | 宏定义 | 调度器后端 | 看门狗配置 |
|------|--------|-----------|-----------|
| `m5stick-c` | (默认) | FreeRTOS | INT_WDT=y (300ms) |
| `m5stick-c-native` | `XEROS_NATIVE_SCHED` | 原生 | INT_WDT=n |
| `native` | `NATIVE_TEST` | ucontext | N/A |

**关键发现:**
- 默认环境 (`m5stick-c`) 启用中断看门狗 (300ms)
- 原生调度器环境禁用中断看门狗
- 用户报告的"看门狗重启"可能来自默认环境

### FreeRTOS 后端架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Xeros 调度器                              │
│  kern_sched_tick() → pick_next_ready() → kern_port_switch_to() │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              kern_port_freertos_switch_to()                  │
│                                                              │
│  1. xSemaphoreGive(g_token_sem[cpu])  → 给任务令牌          │
│  2. xSemaphoreTake(g_done_sem[cpu])   → 等待任务完成        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    FreeRTOS 任务                             │
│  task_wrapper() {                                           │
│    xSemaphoreTake(g_token_sem[cpu])  → 等待令牌             │
│    task->entry(task->arg)            → 执行任务             │
│    kern_exit()                       → 退出                 │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
```

**潜在问题:**
1. 信号量死锁: 如果任务崩溃，`g_done_sem` 永远不会 give
2. 优先级反转: Xeros 任务和 FreeRTOS idle 同优先级，可能饿死 idle
3. 超时处理: 5 秒超时后标记 ZOMBIE，但资源可能未释放

### 原生调度器后端架构

```
┌─────────────────────────────────────────────────────────────┐
│                    kern_sched_tick()                         │
│  xeros_ctx_save(&g_sched_ctx) → 保存调度器上下文            │
│  xeros_ctx_restore(task->native_ctx) → 切换到任务           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    任务执行                                  │
│  ... 任务代码 ...                                           │
│  kern_yield() / kern_exit()                                 │
│    → xeros_ctx_save(task->native_ctx)                       │
│    → xeros_ctx_restore(&g_sched_ctx)                        │
└─────────────────────────────────────────────────────────────┘
```

**潜在问题:**
1. `g_sched_ctx` 是全局变量，SMP 环境下有竞争
2. `xeros_ctx_restore` 中恢复 PS 后 rsync，中断状态可能不一致
3. trampoline 使用 `callx8`，WOE=1 可能导致窗口溢出异常

### 上下文切换汇编分析 (ctx_switch.S)

**xeros_ctx_init:**
- 初始化新任务上下文
- PS = `0x00040020` (WOE=1, UM=1, INTLEVEL=0)
- PC = `xeros_task_trampoline`
- a5 = entry, a6 = arg

**xeros_task_trampoline:**
- 不使用 `entry sp, N`，避免窗口溢出异常
- `mov a10, a6` → 将 arg 放入 call8 第一个参数寄存器
- `callx8 a5` → 调用 entry(arg)
- 返回后调用 `kern_exit(0)`

**xeros_ctx_save:**
- 保存所有 16 个通用寄存器 (a0-a15)
- 保存特殊寄存器 (SAR, LBEG, LEND, LCOUNT, PS, EXCCAUSE, EXCVADDR)
- 保存返回地址到 PC 偏移
- 返回 0 (保存路径)

**xeros_ctx_restore:**
- `rsil a3, 3` → 禁用中断 (INTLEVEL=3)
- 恢复特殊寄存器
- `rsync` → 同步 PS 写入
- 恢复通用寄存器
- `movi a2, 1` → 设置返回值
- `jx a0` → 跳转到保存的 PC

**关键差异 (对比 FreeRTOS port):**

| 特性 | FreeRTOS Port | Xeros Native |
|------|---------------|--------------|
| ABI | call0 | call8 |
| PS 配置 | 禁用 WOE | 启用 WOE |
| 上下文保存 | 手动 (宏) | 汇编函数 |
| 中断处理 | 直接 | 通过 ISR 标志 |
| 窗口管理 | 无 | call8 窗口旋转 |
| 窗口清理 | `xthal_window_spill_nw` | 无 |

### FreeRTOS ESP32 Port 上下文切换实现

参考: [ESP-IDF portasm.S](https://gitlab.informatik.uni-bremen.de/fbrning/esp-idf/-/raw/v3.0.7-rc/components/freertos/portasm.S)

**两种保存模式:**
1. **Solicited (主动 yield)**: 只保存 callee-saved 寄存器 (a12-a15)
2. **Unsolicited (中断)**: 保存完整寄存器集

**窗口溢出处理 (Windowed ABI):**
```asm
movi a6, ~(PS_WOE_MASK|PS_INTLEVEL_MASK)
and a2, a2, a6          /* 清除 WOE 和 INTLEVEL */
addi a2, a2, XCHAL_EXCM_LEVEL  /* 设置 INTLEVEL */
wsr a2, PS
rsync
call0 xthal_window_spill_nw    /* 清理寄存器窗口 */
```

**关键步骤:**
1. 先清除 WOE，防止窗口溢出异常
2. 提高 INTLEVEL 到 EXCM_LEVEL，防止中断
3. 调用 `xthal_window_spill_nw` 清理窗口
4. 恢复 PS 到原始状态

**PS 寄存器处理:**
- 保存时: `rsr a2, PS` → 保存到栈
- 恢复时: `l32i a3, sp, XT_SOL_PS` → `wsr a3, PS`
- 注释: "As soon as PS is restored, interrupts can happen"

### Xeros Native 上下文切换实现

**xeros_ctx_save:**
```asm
entry   sp, 32                  /* call8: 分配帧，旋转窗口 */
s32i    a0, a2, 0               /* 保存 a0 (返回地址) */
s32i    a1, a2, 4               /* 保存 a1 (栈指针) */
... (保存所有寄存器)
rsr     a3, PS
s32i    a3, a2, 80              /* 保存 PS */
s32i    a0, a2, 92              /* 保存 PC = 返回地址 */
movi    a2, 0                   /* 返回 0 */
retw
```

**xeros_ctx_restore:**
```asm
entry   sp, 32                  /* call8: 分配帧，旋转窗口 */
rsil    a3, 3                   /* 禁用中断 (INTLEVEL=3) */
l32i    a3, a2, 80              /* 加载保存的 PS */
wsr     a3, PS                  /* 恢复 PS */
rsync                           /* 同步 */
... (恢复所有寄存器)
movi    a2, 1                   /* 返回 1 */
jx      a0                      /* 跳转到保存的 PC */
```

### 潜在崩溃原因分析

#### 1. 窗口溢出异常 (Window Overflow Exception)

**问题:** Xeros 启用 WOE (PS.WOE=1)，但在上下文切换前没有清理寄存器窗口。

**FreeRTOS 做法:**
- 切换前清除 WOE
- 调用 `xthal_window_spill_nw` 清理窗口
- 防止窗口溢出异常发生在错误时机

**Xeros 问题:**
- `xeros_ctx_save` 的 `entry sp, 32` 可能触发窗口溢出
- 如果溢出处理器引用无效的 SP，会导致崩溃

**修复方案:**
1. 在 `xeros_ctx_save` 前禁用 WOE
2. 调用 `xthal_window_spill_nw` 清理窗口
3. 或者切换到 Call0 ABI

#### 2. 中断状态不一致

**问题:** `xeros_ctx_restore` 中先恢复 PS，再 rsync，然后恢复寄存器。

**风险:**
- PS 恢复后，中断可能立即到达
- 此时寄寄存器还未完全恢复
- 可能导致状态不一致

**FreeRTOS 做法:**
- 在恢复 PS 前提高 INTLEVEL
- 使用 exit dispatcher 确保原子恢复

**修复方案:**
1. 在恢复 PS 前禁用中断
2. 使用原子恢复序列

#### 3. trampoline 窗口管理

**问题:** `xeros_task_trampoline` 使用 `callx8` 调用 entry。

**风险:**
- `callx8` 设置 CALLINC=2，旋转窗口
- 如果此时 WOE=1，可能触发窗口溢出
- 溢出处理器引用无效 SP → 崩溃

**FreeRTOS 做法:**
- 使用 Call0 ABI，无窗口旋转
- 或在切换前清理窗口

**修复方案:**
1. 在 trampoline 前禁用 WOE
2. 或使用 Call0 ABI

---

## 崩溃复现 (2026-06-25)

### 崩溃日志

```
[INFO] switch_to pid=3 name=wifi-mgr
[D] restore ctx: pc=1074270580 a0=1074270580 a1=1073601840 a5=1074630368 a6=0 a13=1074630368 a14=0 ps=0x00040020
Guru Meditation Error: Core  0 panic'ed (InstrFetchProhibited). Exception was unhandled.

Core  0 register dump:
PC      : 0x800dc90d  PS      : 0x00060630  A0      : 0x800dc90d  A1      : 0x3ffc2e30
A2      : 0x00000001  A3      : 0x40081174  A4      : 0x40081174  A5      : 0x3ffddd30
A6      : 0x400d8ee0  A7      : 0x00000000  A8      : 0x800dac50  A9      : 0x3ffc2de0
A10     : 0x00000000  A11     : 0x3ffc2e30  A12     : 0x3ffc2e10  A13     : 0x00000004
A14     : 0x00000008  A15     : 0x00000000  SAR     : 0x00000005  EXCCAUSE: 0x00000014
EXCVADDR: 0x800dc90c  LBEG    : 0x4000c2e0  LEND    : 0x4000c2f6  LCOUNT  : 0xffffffff

Backtrace: 0x400dc90a:0x3ffc2e30 0x400dc90a:0x3ffddcd0 0x400d88c8:0x3ffddcf0 0x400d8ee8:0x3ffddd10 0x40081176:0x3ffddd30 0x40081171:0x3ffc2e50 0x400e0278:0x3ffc2e80 0x400f784b:0x3ffc2ea0 0x40194dea:0x3ffc2ec0
```

### 崩溃分析

| 寄存器 | 值 | 含义 |
|--------|-----|------|
| EXCCAUSE | 0x00000014 | InstrFetchProhibited (指令取指被禁止) |
| EXCVADDR | 0x800dc90c | 尝试从无效地址取指令 |
| PC | 0x800dc90d | 程序计数器指向无效地址 |

**ESP32 内存映射:**
- `0x3FFxxxxx` - DRAM (数据)
- `0x400xxxxx` - IRAM (指令)
- `0x400Dxxxx` - Flash (指令)
- `0x800xxxxx` - **无效区域** (会导致异常)

### 上下文切换日志

```
switch_to pid=3 name=wifi-mgr
restore ctx: pc=1074270580 = 0x40081174 (IRAM - 有效)
             a0=1074270580 = 0x40081174 (与 pc 相同)
             a1=1073601840 = 0x3FFC2E60 (栈指针)
             a5=1074630368 = 0x400DC720 (entry 函数)
             ps=0x00040020 (WOE=1, UM=1, INTLEVEL=0)
```

**问题:** PC 从有效地址 `0x40081174` 跳转到了无效地址 `0x800dc90d`

### 根本原因分析

#### 假设 1: 窗口溢出异常 (Window Overflow Exception)

**证据:**
- PS.WOE=1 (启用窗口溢出异常)
- `xeros_ctx_restore` 使用 `entry sp, 32` 旋转窗口
- 如果旧窗口的 SP 无效，会触发窗口溢出异常

**问题:**
- `xeros_ctx_restore` 在 `entry` 后旋转窗口
- 此时 a2 = 原来的 a10 = ctx 指针
- 但 `entry` 可能触发窗口溢出，如果溢出处理器引用无效 SP

**验证:**
- 检查 `xeros_task_trampoline` 是否正确初始化了栈
- 检查 `xeros_ctx_init` 是否设置了正确的 PS

#### 假设 2: trampoline 窗口管理

**证据:**
- `xeros_task_trampoline` 使用 `callx8 a5` 调用 entry
- `callx8` 设置 CALLINC=2，旋转窗口
- 如果此时 WOE=1，可能触发窗口溢出

**问题:**
- trampoline 从 `xeros_ctx_restore` 恢复后执行
- 此时 PS.WOE=1
- `callx8` 旋转窗口，可能触发溢出

**验证:**
- 检查 trampoline 是否禁用了 WOE
- 检查 FreeRTOS 如何处理这个问题

#### 假设 3: 上下文结构体被破坏

**证据:**
- 恢复前 pc=0x40081174 (有效)
- 恢复后 PC=0x800dc90d (无效)
- 差值: 0x800dc90d - 0x40081174 = 0x4005B799

**问题:**
- ctx 结构体可能被栈溢出破坏
- 或者内存损坏

**验证:**
- 检查 wifi-mgr 任务的栈大小
- 检查是否有栈溢出保护

### 修复方案

#### 方案 1: 禁用 WOE (推荐)

在 `xeros_ctx_restore` 前禁用 WOE，防止窗口溢出异常：

```asm
xeros_ctx_restore:
    entry   sp, 32
    
    /* 禁用 WOE，防止窗口溢出异常 */
    rsr     a3, PS
    movi    a4, ~(1 << 18)  /* PS_WOE_MASK */
    and     a3, a3, a4
    wsr     a3, PS
    rsync
    
    /* 继续恢复... */
```

#### 方案 2: 使用 Call0 ABI

将所有上下文切换函数改为 Call0 ABI，避免窗口旋转：

```asm
    .option call0
xeros_ctx_save:
    /* 无需 entry，直接保存寄存器 */
    ...
```

#### 方案 3: 清理窗口 (参考 FreeRTOS)

在切换前调用 `xthal_window_spill_nw` 清理窗口：

```asm
xeros_ctx_restore:
    entry   sp, 32
    
    /* 清理窗口 */
    call0   xthal_window_spill_nw
    
    /* 继续恢复... */
```

### 下一步行动

1. **验证假设 1**: 检查 `xeros_ctx_init` 中的 PS 设置
2. **验证假设 2**: 检查 trampoline 是否禁用了 WOE
3. **实现修复**: 尝试方案 1 (禁用 WOE)
4. **测试**: 重新构建并测试

---

## 调试笔记

### 关于 Xtensa call8 ABI

ESP32 使用 Xtensa LX6 处理器，支持 windowed register file:
- 16 个通用寄存器 (a0-a15)
- `entry sp, N` 指令旋转寄存器窗口
- call8 ABI: 参数通过 a10-a15 传递，entry 后映射到 a2-a7
- WOE (Window Overflow Enable): 启用时，窗口溢出会触发异常

### 关于 FreeRTOS ESP32 Port

FreeRTOS ESP32 port 使用:
- call0 ABI (非 windowed)
- 通过 `portSAVE_CONTEXT` / `portRESTORE_CONTEXT` 宏保存/恢复上下文
- 使用 `xt_set_interrupt_handler` 注册中断处理程序
- 中断看门狗 (INT_WDT) 监控 idle 任务是否在 300ms 内喂狗

### 关键差异

| 特性 | FreeRTOS Port | Xeros Native |
|------|---------------|--------------|
| ABI | call0 | call8 |
| 上下文保存 | 手动 (宏) | 汇编函数 |
| 窗口管理 | 禁用 WOE | 启用 WOE |
| 中断处理 | 直接 | 通过 ISR 标志 |

---

## 根因分析：callx8 + entry 双重窗口旋转导致寄存器映射错误 (2026-06-24)

### 问题描述

修复 WOE 后，系统仍然崩溃，报 `IllegalInstruction` 或 `InstrFetchProhibited`。
根本原因不是 WOE，而是 `callx8` + `entry sp,32` 的**双重窗口旋转**导致 callee 收到错误的栈指针和参数。

### 寄存器映射分析

*Xtensa call8 ABI 窗口旋转机制：*

```
callx8 a5：旋转窗口 +8（CALLINC=2 × 4 = 8）
entry sp,32：再旋转 +8（CALLINC=2 × 4 = 8）
总计：+16 = 回到原始物理寄存器窗口
```

这意味着 callee 的寄存器**直接映射到 trampoline 的原始寄存器（即 ctx 结构体中的值）**：

| callee 寄存器 | 物理寄存器 | 来源 | 修复前的值 | 修复后 |
|---|---|---|---|---|
| a0 (返回地址) | W+0 | callx8 自动写入 | ✓ 正确 | ✓ |
| a1 (栈指针) | W+1 | entry: ctx->a9 - 32 | **0 - 32 = 无效!** | stack_top - 32 ✓ |
| a2 (第一个参数) | W+2 | ctx->a2 | **0 (丢失!)** | arg ✓ |

### 中文伪代码拆解

```
函数 xeros_ctx_init_assembler(ctx, stack_base, stack_size, entry, arg) {

    // 第一步：清零上下文
    memset(ctx, 0, sizeof(ctx))

    // 第二步：计算栈顶
    栈顶 = (stack_base + stack_size) & ~0xF

    // 第三步：设置 trampoline 相关寄存器
    ctx->a0 = trampoline地址     // 返回地址
    ctx->a1 = 栈顶              // trampoline 自己的 sp
    ctx->a5 = entry             // 蹦床 callx8 的目标
    ctx->a6 = arg               // 蹦床 mov a10, a6 的源
    ctx->pc = trampoline地址    // 首次恢复的执行入口
    ctx->ps = 0x00000020        // 用户模式，WOE=0

    // ★ 关键修复：双重旋转后的 callee 寄存器映射 ★
    //
    // callx8 a5 把窗口旋转了 8 个寄存器
    // callee 的 entry sp,32 又旋转了 8 个
    // 总共旋转 16 = 回到原点
    // 所以 callee 的 a1 = ctx->a9, a2 = ctx->a2

    ctx->a2 = arg               // callee 的第一个参数
    ctx->a9 = 栈顶              // callee 的 entry 会做 sp = a9 - 32
}
```

### 修复内容

**文件 `ctx_init.c`（C 封装版）：**
- 新增 `ctx->a2 = (uint32_t)arg`
- 新增 `ctx->a9 = stack_top`

**文件 `ctx_switch.S`（汇编版）：**
- 新增 `s32i a6, a2, 8`（ctx->a2 = arg）
- 新增 `s32i a3, a2, 36`（ctx->a9 = stack_top）

### 修复原理

callx8 旋转 +8 后，callee 的 a1 = trampoline 的 a9（= ctx->a9）。
callee 的 entry sp,32 执行 `sp = a1 - 32`。
如果 a9=0，则 sp = -32 = 0xFFFFFFE0（无效地址）→ 崩溃。

修复后 a9 = stack_top，所以 sp = stack_top - 32（正确的满递减栈初始位置）。

同理，callee 的 a2 = ctx->a2。如果不设置，callee 收到 arg=0 而非正确参数。

### 状态

- [x] WOE 修复（PS = 0x00000020）
- [x] 双重旋转寄存器映射修复（ctx->a2 = arg, ctx->a9 = stack_top）
- [ ] 实机验证（需要烧录测试）

---

## 会话 3: CALLINC=1 call4 trampoline 实现与调试 (2026-06-25)

### 思路

将上下文切换从 callx8+entry（双重窗口旋转）改为 call4 trampoline + C wrapper 方案，
与 FreeRTOS ESP32 port 的 vPortTaskWrapper 模式一致。

### 实现内容

**ctx_switch.S 重写：**
- `xeros_ctx_save`: 无 entry 指令，停留在 kern_sched_tick 的寄存器窗口
- `xeros_ctx_restore`: call8 薄包装器 → call0 实现
- `xeros_ctx_restore_impl`: call0 ABI，无窗口旋转
- `xeros_task_trampoline`: entry sp, 0 (CALLINC=1) + call4 xeros_task_wrapper
- PS = 0x00040023 (CALLINC=1, WOE=1, UM=1, INTLEVEL=3)

**寄存器映射（ctx → 蹦床 → 包装函数）：**
- ctx->a2 = 清理处理器 → 蹦床 a2 → 包装函数 a0 (retw 返回地址)
- ctx->a3 = 栈顶 → 蹦床 a3 → 包装函数 a1 (SP)
- ctx->a4 = entry 函数 → 蹦床 a4 → 包装函数 a2 (pxCode)
- ctx->a5 = arg → 蹦床 a5 → 包装函数 a3 (pvParameters)

### 崩溃日志 (第一次实机测试)

```
[  OK  ] Kernel boot complete, entering scheduler loop
RNGuru Meditation Error: Core  0 panic'ed (Double exception).

PC      : 0x40097cee  PS      : 0x00040636
EXCCAUSE: 0x00000002  (InstrFetchProhibited)
EXCVADDR: 0xffffffe0  (-32 = 无效地址)

Backtrace: 0x40097ceb:_xt_context_save 0x400dc8c1:kern_sched_tick
```

### 崩溃分析

**关键观察：**
1. `EXCVADDR = 0xffffffe0` (-32) — 典型的空/无效栈指针问题
2. 输出 "RNG" — 调试标记 'R' (ctx_restore_impl) + 'N' (新任务路径)
3. Double Exception — 异常处理器本身崩溃（栈指针无效）
4. Backtrace 显示 `_xt_context_save` — ESP-IDF 异常处理器的上下文保存

**根本原因：**

`ctx_restore_impl` 的新任务路径没有加载 a1（栈指针）！

```asm
.L_new_task:
    l32i    a0, a2, 92    /* a0 = trampoline 地址 */
    /* ★ 缺少: l32i a1, a2, 4  — 没有加载栈指针！ */
    l32i    a3, a2, 12    /* a3 = stack_top */
    ...
    jx      a0            /* 跳转到蹦床 */
```

当蹦床执行 `entry sp, 0` (CALLINC=1) 时：
1. 窗口旋转：WB = K → K+1
2. 旧窗口 (K) 的寄存器需要刷写到内存
3. 刷写使用旧窗口的 SP（物理寄存器 R[K*4+1]）
4. **该 SP 从未被加载，是垃圾值 0**
5. 写入地址 0 - 32 = 0xffffffe0 → 触发异常
6. 异常处理器尝试保存上下文，但 SP 也是无效的 → Double Exception

### 修复

**ctx_init.c / ctx_switch.S：**
- `ctx->a1 = stack_top` — 初始化时设置有效的栈指针
- `ctx_restore_impl`: `l32i a1, a2, 4` — 恢复时加载栈指针

### 状态

- [x] CALLINC=1 call4 trampoline 实现
- [x] a1 (SP) 加载修复
- [ ] 实机验证（设备断开，需要重新连接）

---

### 会话 4: xthal_window_spill_nw 修复 (2026-06-25)

### 思路

研究 FreeRTOS ESP32 port 的上下文切换实现，发现关键差异：
FreeRTOS 在切换上下文前调用 `xthal_window_spill_nw` 刷写所有寄存器窗口，
而我们的实现没有这一步骤。

### FreeRTOS 的关键模式

```asm
/* FreeRTOS vPortYield 中的窗口刷写 (portasm.S:568-579) */
movi    a6,  ~(PS_WOE_MASK|PS_INTLEVEL_MASK)  /* 清除 WOE 和 INTLEVEL */
and     a2,  a2, a6
addi    a2,  a2, XCHAL_EXCM_LEVEL             /* 设置 INTLEVEL */
wsr     a2,  XT_REG_PS
rsync
call0   xthal_window_spill_nw                 /* 刷写所有窗口 */
l32i    a2,  sp, XT_SOL_PS                    /* 恢复 PS */
wsr     a2,  XT_REG_PS
```

### 崩溃分析 (第二次实机测试)

```
[  OK  ] Kernel boot complete, entering scheduler loop
RNGuru Meditation Error: Core  0 panic'ed (Double exception).

PC      : 0x40097cee  PS      : 0x00040636
EXCCAUSE: 0x00000002  (InstrFetchProhibited)
EXCVADDR: 0x0000024a  (低地址，可能是旋转后的寄存器映射问题)

Backtrace: 0x40097ceb:_xt_context_save 0x400dc8c1:kern_sched_tick
```

**关键观察：**
1. EXCVADDR 从 0xffffffe0 变为 0x0000024a — SP 修复部分生效
2. A0 = 0x800dc8c8 — 带有 CALLINC 位的地址（0x80xxxxxx = CALLINC=2）
3. 仍然是 Double Exception — 异常处理器崩溃

**根本原因分析：**

蹦床的 `entry sp, 0` (CALLINC=1) 会旋转窗口。旋转时，旧窗口的寄存器
需要刷写到内存。如果旧窗口的 SP 无效或内存不可写，就会触发异常。

问题在于：我们没有在恢复上下文前刷写所有寄存器窗口。当蹦床旋转窗口时，
硬件尝试刷写旧窗口，但旧窗口的 SP 可能指向无效内存。

### 修复

**ctx_switch.S：**
在 `xeros_ctx_restore_impl` 开头添加 `xthal_window_spill_nw` 调用：

```asm
/* Step 1: Spill all register windows (FreeRTOS pattern) */
mov     a15, a2                 /* 保存 ctx 指针到 callee-saved a15 */
rsr     a3, PS                  /* 读取当前 PS */
/* 清除 WOE (bit 18 = 0x40000) */
movi    a4, 0x40
slli    a4, a4, 12              /* a4 = 0x00040000 */
movi    a5, -1
xor     a4, a5, a4              /* a4 = ~0x00040000 = 0xFFFBFFFF */
and     a3, a3, a4              /* 清除 WOE */
/* 清除 INTLEVEL (bits 3:0) 并设置为 3 */
movi    a4, 0xF
xor     a4, a5, a4              /* a4 = ~0xF = 0xFFFFFFF0 */
and     a3, a3, a4              /* 清除 INTLEVEL */
addi    a3, a3, 3               /* INTLEVEL=3 (屏蔽中断) */
wsr     a3, PS
rsync
call0   xthal_window_spill_nw   /* 刷写所有窗口到内存 */
mov     a2, a15                 /* 恢复 ctx 指针 */
```

### 原理

1. **清除 WOE**：防止刷写过程中触发窗口溢出异常
2. **设置 INTLEVEL=3**：屏蔽中断，防止刷写过程中被中断
3. **调用 xthal_window_spill_nw**：将所有寄存器窗口刷写到内存
4. **恢复 ctx 指针**：因为 xthal_window_spill_nw 会破坏 a2-a5

刷写后，所有窗口都在内存中，蹦床的 `entry sp, 0` 旋转窗口时
不会触发溢出异常（因为窗口已经在内存中了）。

### 状态

- [x] CALLINC=1 call4 trampoline 实现
- [x] a1 (SP) 加载修复
- [x] xthal_window_spill_nw 修复（已撤回，见会话 5）

### 下次调试排除路径

1. ✅ ~~禁用 WOE~~ — 不是 WOE 的问题，是 SP 未加载
2. ✅ ~~call0 ABI~~ — call4 trampoline 方案可行，只需修复 SP
3. ✅ ~~ctx 结构体被破坏~~ — 结构体本身没问题，是寄存器未加载
4. 待验证：蹦床的 entry sp, 0 窗口旋转是否还需要其他修复

---

## 会话 5: xthal_window_spill_nw 反汇编分析与撤回 (2026-06-25)

### 思路

会话 4 添加了 `xthal_window_spill_nw` 调用，但设备仍未验证。进一步分析该函数的
实际实现，发现它**不适合在上下文切换代码中使用**。

### 反汇编分析

对 ESP-IDF 中的 `xthal_window_spill_nw` 进行反汇编分析，发现：

```
xthal_window_spill_nw:
    # 注意：没有 entry 指令！
    # 内部使用 rotw 旋转窗口
    # 使用 a9 作为 SP 链的基址
    ...
```

**关键发现：**

1. **无 entry 指令**：该函数使用 `call0` 调用约定，不旋转窗口
2. **内部使用 rotw**：手动旋转窗口来遍历所有窗口
3. **依赖 a9 寄存器链**：使用 a9 作为每个窗口的 SP 来计算刷写目标地址
4. **假设正常调用链**：a9 由正常的 `entry` 指令在函数调用时设置

### 问题所在

我们的上下文切换通过 `ctx_save` 创建窗口，**不是**通过正常的函数调用链。
这意味着：

```
正常函数调用链：
  main() → task_A() → task_B()
  每个 entry 指令都会设置 a9 = sp - N
  a9 链正确，xthal_window_spill_nw 可以找到每个窗口的 SP

我们的 ctx_save：
  kern_sched_tick() → ctx_save() → [保存寄存器到 ctx]
  ctx 中的 a9 值可能是 0 或垃圾值
  xthal_window_spill_nw 使用垃圾 a9 作为刷写目标 → 内存损坏！
```

### 中文伪代码拆解

```
函数 xthal_window_spill_nw() {

    // 该函数没有 entry 指令，使用 call0 ABI
    // 内部逻辑：

    当前WB = 读取 WINDOWBASE 寄存器

    遍历每个窗口 (i = 0 到 15) {
        目标WB = (当前WB + i) % 16

        // ★ 关键问题：使用 a9 作为该窗口的 SP
        // a9 在正常调用链中由 entry 指令设置
        // 但我们通过 ctx_save 创建的窗口没有正确的 a9！

        SP_该窗口 = a9  // ← 这个 a9 可能是垃圾值！

        if (窗口脏了) {
            将 a0-a3 写入到 SP_该窗口 - 16  // ← 写入垃圾地址！
        }

        rotw(1)  // 旋转到下一个窗口
    }
}
```

### 修复

**移除 `xthal_window_spill_nw` 调用**。改为依赖蹦床的 `entry sp, 0` 指令
触发的窗口溢出异常处理器来正确刷写旧窗口。

```asm
/* 旧代码（已移除）：*/
mov     a15, a2
rsr     a3, PS
/* ... 清除 WOE, 设置 INTLEVEL=3 ... */
wsr     a3, PS
rsync
call0   xthal_window_spill_nw   /* ← 危险！a9 链不正确 */
mov     a2, a15
rsil    a3, 3

/* 新代码：*/
rsil    a3, 3                   /* INTLEVEL = XCHAL_EXCM_LEVEL */
```

### 窗口溢出异常处理器的工作原理

当蹦床执行 `entry sp, 0` (CALLINC=1) 时：

1. 窗口旋转：WB = K → K+1
2. 旧窗口 (K) 的寄存器需要刷写到内存
3. 硬件触发窗口溢出异常
4. **异常处理器使用旧窗口的 SP（物理寄存器 R[K*4+1]）**
5. 我们在 ctx_restore_impl 中加载了 `a1 = ctx->a1 = stack_top`
6. stack_top 是有效的栈地址 → 刷写成功！

**关键区别：**
- `xthal_window_spill_nw` 使用 **a9** 作为 SP（需要正确的调用链）
- 窗口溢出异常处理器使用 **物理寄存器 R[K*4+1]**（即旧窗口的 a1）
- 我们确保了 ctx->a1 = stack_top，所以旧窗口的 a1 是有效的

### 下次调试排除路径（更新）

1. ✅ ~~禁用 WOE~~ — 不是 WOE 的问题
2. ✅ ~~call0 ABI~~ — call4 trampoline 方案可行
3. ✅ ~~ctx 结构体被破坏~~ — 结构体本身没问题
4. ✅ ~~xthal_window_spill_nw~~ — 不安全，a9 链不正确
5. 待验证：蹦床的 entry sp, 0 窗口溢出异常处理器能否正确刷写旧窗口
6. 待验证：WINDOWSTART 设置为 0xFFFF 是否能防止窗口下溢异常

### 状态

- [x] CALLINC=1 call4 trampoline 实现
- [x] a1 (SP) 加载修复
- [x] xthal_window_spill_nw 移除（不安全）
- [x] WINDOWSTART = 0xFFFF 设置
- [ ] 实机验证（需要烧录测试）

---

### 会话 7: CALLINC、WINDOWSTART 和调用约定修复 (2026-06-25)

**思路:** 继续调试，修复三个关键问题。

#### 问题 1: PS 常量中 CALLINC=0

**现象:** PS 值 `0x00040023` 的 bits[17:16] = 00 (CALLINC=0)，但注释声称 CALLINC=1。

**影响:** 蹦床的 `entry sp, 0` 指令在 CALLINC=0 时行为未定义，可能触发
IllegalInstruction 异常。

**修复:** 将 PS 常量改为 `0x00050023` (bits[17:16] = 01, CALLINC=1)。
修改位置：
- `ctx_switch.S` 行 92 (rodata 常量)
- `ctx_switch.S` 行 188-189 (xeros_ctx_init)
- `ctx_switch.S` 行 510 (ctx_restore_impl 新任务 PS 计算)
- `ctx_init.c` 行 135 (xeros_ctx_init_assembler)

#### 问题 2: WINDOWSTART=0xFFFF 导致无法修复的溢出

**现象:** 设置 `WINDOWSTART=0xFFFF` 标记所有 16 个窗口为有效，导致每次旋转
都触发窗口溢出。溢出处理器使用 OWB 窗口的 a5 作为 S32E 的基址。

**分析:** 我们只能从当前窗口 (W) 设置 3 个窗口的 a5：
- ctx->a5 (offset 20) → 窗口 W 的 a5
- ctx->a9 (offset 36) → 窗口 W+1 的 a5
- ctx->a13 (offset 52) → 窗口 W+2 的 a5

窗口 W+3 到 W+15 的 a5 值无法控制（memset 清零或未初始化）。
任何旋转到这些窗口都会用无效的 a5 执行 S32E → 写入 0xFFFFFFF0 → 崩溃。

**修复:** 替换为定向 3 窗口掩码：
```asm
rsr     a3, WINDOWBASE          /* a3 = 当前 WB (0-15) */
movi    a4, 1
movi    a5, 0                   /* a5 = 累加器 */
ssl     a3                      /* SAR = WB */
sll     a6, a4                  /* a6 = 1 << WB */
or      a5, a5, a6
addi    a3, a3, 1
extui   a3, a3, 0, 4            /* a3 = (WB+1) & 0xF */
ssl     a3
sll     a6, a4                  /* a6 = 1 << ((WB+1) & 0xF) */
or      a5, a5, a6
addi    a3, a3, 1
extui   a3, a3, 0, 4            /* a3 = (WB+2) & 0xF */
ssl     a3
sll     a6, a4                  /* a6 = 1 << ((WB+2) & 0xF) */
or      a5, a5, a6              /* a5 = (1<<WB)|(1<<(WB+1))|(1<<(WB+2)) */
wsr     a5, WINDOWSTART
rsync
```

**为什么 3 个窗口足够:**
蹦床执行两次 CALLINC=1 旋转（entry + callx4）：
1. K→K+1: 溢出保存 K，使用 a5 from K (= ctx->a5 = stack_top) ✓
2. K+1→K+2: 溢出保存 K+1，使用 a5 from K+1 (= ctx->a9 = stack_top) ✓
3. K+2→K+3: WINDOWSTART[K+3]=0，无溢出 ✓

#### 问题 3: wrapper 使用 callx8 调用 call4 函数（根本原因）

**现象:** 即使修复了 CALLINC 和 WINDOWSTART，设备仍然崩溃。

**根本原因分析:**

通过反汇编发现 wrapper 编译为 call4 ABI（entry 指令的 CALLINC=1），但内部使用
`callx8` 调用 pxCode：

```
xeros_task_wrapper:
   0:	004136    entry   a1, 32      ; CALLINC=1 (call4)
   3:	03ad      mov.n   a10, a3     ; a10 = pvParameters
   5:	0002e0    callx8  a2          ; callx8 pxCode ← 问题！
   8:	000081    l32r    a8, ...
   b:	0008e0    callx8  a8          ; callx8 kern_exit
   e:	f01d      retw.n
```

`callx8` 设置 PS.CALLINC=2 并将返回地址存入 a8。但 pxCode（如 `idle_entry`）
是 call4 函数，其 `retw.n` 从 a0 加载返回地址（CALLINC=1）。

**返回地址不匹配:**
- callx8 存储: a8 (CALLINC=2)
- call4 的 retw.n 加载: a0 (CALLINC=1)
- 结果: 跳转到错误地址 → 崩溃

**修复:** 将 wrapper 移到汇编中（ctx_switch.S），显式使用 `callx4`：

```asm
xeros_task_wrapper:
    entry   a1, 16              /* CALLINC=1: rotate +1 */
    mov     a6, a2              /* a6 = pxCode */
    mov     a2, a3              /* a2 = pvParameters */
    callx4  a6                  /* CALLINC=1: call pxCode */
    movi    a2, kern_exit
    callx4  a2                  /* CALLINC=1: call kern_exit */
```

同时将蹦床从 `call4 xeros_task_wrapper` 改为 `callx4`（通过 literal pool），
确保跨 IRAM/flash 段的调用可靠。

### 最终状态

- [x] CALLINC=1 PS 常量修复 (0x00040023 → 0x00050023)
- [x] WINDOWSTART 定向 3 窗口掩码（替换 0xFFFF）
- [x] wrapper 改为汇编实现，使用 callx4（修复 callx8/call4 不匹配）
- [x] trampoline 改为 callx4（跨段可靠调用）
- [x] 移除调试标记

---

### 会话 8: UI 任务不执行 — WINDOWSTART 掩码不足 (2026-06-25)

**现象:** 看门狗崩溃修复后，设备正常启动到 "Kernel boot complete, entering scheduler loop"，
但 UI 任务和 idle 任务从未执行（无 UART 输出）。

**对比 FreeRTOS 版本:**
- FreeRTOS 版本的 `main.cpp` 主循环与原生调度器共享同一代码路径
- `kern_port_preempt_consume()` 通过 `tick_timer_consume()` 工作（GPTimer ISR 设置标志）
- `kern_mpu_apply()` 是空操作（ESP32 无 MPU）
- 调度逻辑本身正确：RR 时间片递减 → `g_need_resched = true` → `pick_next_ready()`

**根因分析:**

WINDOWSTART 掩码只设置了 3 个窗口位（K, K+1, K+2），但完整的调用链需要 5 个窗口：

```
ctx_restore_impl (WB=K)
  → jx trampoline (WB=K)
    → entry sp, 0 (WB=K→K+1, 旋转 +1)
      → callx4 wrapper (WB=K+1→K+2, 旋转 +1)
        → entry a1, 16 (WB=K+2→K+3, 旋转 +1)  ← K+3 不在 WINDOWSTART 中!
          → callx4 pxCode (WB=K+3→K+4, 旋转 +1)
```

当 wrapper 的 `entry` 指令尝试旋转到 K+3 时，WINDOWSTART 的 bit K+3 = 0，
触发窗口下溢异常（window underflow exception）。异常处理器尝试从溢出区恢复寄存器，
但该窗口从未被溢出保存，导致加载垃圾数据 → 崩溃或挂起。

**代码中的矛盾:**
- 注释（第 83 行）说 "WINDOWSTART = 0xFFFF ensures all windows are valid"
- 但实际代码只设置了 3 个位
- FreeRTOS 使用 0xFFFF 设置所有 16 个窗口位

**修复:** 将 WINDOWSTART 从定向 3 窗口掩码改为 0xFFFF（所有 16 个窗口有效）：

```asm
/* 修复前：只设置 3 个窗口 */
rsr     a3, WINDOWBASE
movi    a4, 1
movi    a5, 0
ssl     a3
sll     a6, a4
or      a5, a5, a6
/* ... 设置 WB+1, WB+2 ... */
wsr     a5, WINDOWSTART

/* 修复后：设置所有 16 个窗口 */
movi    a3, 0xFFFF
wsr     a3, WINDOWSTART
```

0xFFFF 是安全的：溢出处理器使用 a5 = stack_top（由 ctx_init 设置）作为保存基址，
即使未使用的窗口被溢出，也只是将栈顶附近的内存写入溢出区（无害）。

**验证方法:** 在 kern_sched_tick()、idle_entry()、ui_task_main() 中添加直接 UART 输出
（写入 0x3FF40000），追踪调度器是否在运行、任务是否被选中、上下文切换是否成功。

### 会话 8 最终状态

- [x] WINDOWSTART 改为 0xFFFF（修复窗口下溢异常）
- [x] 调试输出已添加（待设备连接后验证）
- [ ] 需要刷入固件并验证 UI 任务正常执行
- [x] 实机验证：设备稳定启动，30 秒无 watchdog 崩溃 ✅

---

## 会话 9: rsil 特权指令崩溃 + self-switch guard (2026-06-25)

### 现象

设备稳定启动后，串口输出始终停在 `T>sR`：
- `T` = kern_sched_tick 入口
- `>` = pick_next_ready 返回
- `s` = 选中 shell 任务（name[0]='s'）
- `R` = 即将调用 xeros_ctx_restore

之后无任何输出，shell 从未执行，idle 也从未执行。

### 根因分析

通过逐步添加 UART 调试点，定位到 `xeros_ctx_restore_impl` 入口处：

```asm
xeros_ctx_restore_impl:
    movi    a3, 0x3FF40000
    movi    a4, 48                  /* '0' */
    s8i     a4, a3, 0              /* ← 输出 '0'，成功 */

    rsil    a3, 3                   /* ← 执行后挂起！ */

    movi    a3, 0x3FF40000
    movi    a4, 65                  /* 'A' */
    s8i     a4, a3, 0              /* ← 从未执行 */
```

**`rsil` (Read and Set Interrupt Level) 是 Xtensa 特权指令**，只能在以下条件执行：
- PS.UM = 0（内核模式）
- PS.EXCM = 1（异常模式）

调度器从 `kern_smp_sched_loop`（FreeRTOS 任务）中调用，运行在**用户模式 (PS.UM=1)**。
执行 `rsil` 触发 PrivilegedInstructionException，异常处理程序死循环。

### 修复

**ctx_switch.S：移除 `rsil a3, 3`**

后续的 PS 写入（`wsr a3, PS`）已设置 INTLEVEL=3，覆盖整个上下文恢复流程。
`rsil` 是多余的，且在用户模式下非法。

在 PS 写入之前的几条指令（SAR/LBEG/LEND/LCOUNT/WINDOWSTART 恢复）期间，
即使有中断到达，中断处理器也会正确保存和恢复所有寄存器，不影响正确性。

**kern_sched.c：添加 self-switch guard**

```c
// 修复前：
if (next) {

// 修复后：
if (next && next != g_current_task) {
```

缺少 guard 时，`pick_next_ready()` 返回当前任务会导致不必要的
`xeros_ctx_save`/`xeros_ctx_restore` 往返，新任务路径会从头重启。

### 状态

- [x] rsil 特权指令移除
- [x] self-switch guard 添加
- [ ] 实机验证（需要烧录测试）
