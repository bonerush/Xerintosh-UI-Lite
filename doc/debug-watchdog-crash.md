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
