# Xeros 内核子系统总览

> **Parent:** [知识地图](../index.md) | **Related:** [类型系统](kern-types.md), [调度器](kern-task.md), [VFS](kern-vfs.md)

## 概述

Xeros 是一个轻量级**协作式微内核**，运行在 M5Stick-C (ESP32-PICO, 520KB SRAM) 上。它的设计受到 Unix 哲学启发 — "一切皆文件"、进程隔离、系统调用接口 — 但为了嵌入式场景做了极简化。

核心设计目标：
1. **协作式调度**：手写 Round-Robin 调度器，任务主动让出 CPU，无需复杂锁机制
2. **一切皆文件**：通过 VFS 将显示、按键、串口等硬件抽象为 `/dev/fb0`、`/dev/input0`、`/dev/ttyS0`
3. **动态栈管理**：每个任务从 1KB 起始栈分配，按需扩展到上限 8KB，内置金丝雀溢出检测
4. **IPC 机制**：匿名 pipe（环形缓冲区）和命名消息队列，支持任务间数据传递

FreeRTOS 继续在底层为 WiFi/BT 协议栈服务，Xeros 运行在 Arduino `loop()` 的单一线程内，不创建新的 FreeRTOS 任务，也不与底层冲突。

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│ 用户态任务                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│  │ ui_task  │ │wifi_task │ │ bt_task  │ │shell_task│        │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘        │
│       └────────────┴────────────┴────────────┘              │
│              sys_open / sys_read / sys_write / sys_yield    │
├─────────────────────────────────────────────────────────────┤
│ Xeros 内核                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│  │Scheduler │ │   VFS    │ │  devfs   │ │  procfs  │        │
│  │ (coop)   │ │(inode)   │ │(/dev/*)  │ │(/proc/*) │        │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│  │  sysfs   │ │   IPC    │ │  Shell   │ │ Syscall  │        │
│  │(/sys/*)  │ │(pipe/mq) │ │(30+ cmd) │ │dispatch  │        │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘        │
│  ┌───────────┐ ┌───────────┐                                  │
│  │  gpiofs   │ │  Scope    │                                  │
│  │ /sys/gpio │ │(监测引擎) │                                  │
│  └───────────┘ └───────────┘                                  │
├─────────────────────────────────────────────────────────────┤
│ HAL / FreeRTOS（底层，不动）                                │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│  │M5Unified │ │M5GFX     │ │WiFi Stack│ │BT Stack  │        │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘        │
└─────────────────────────────────────────────────────────────┘
```

## 模块导航

| 模块 | 文档 | 源码 | 核心职责 |
|------|------|------|----------|
| 类型系统 | [kern-types.md](kern-types.md) | `kern_types.h` | 错误码（errno 子集）、PID、任务状态、日志级别、栈常量 |
| 初始化与日志 | [kern-init.md](kern-init.md) | `kern_init.c/h` | 内核启动入口、分级日志、panic 处理 |
| 协作式调度器 | [kern-task.md](kern-task.md) | `kern_task.c/h` | TCB、Round-Robin、动态栈、yield/sleep/exit |
| VFS 核心 | [kern-vfs.md](kern-vfs.md) | `kern_vfs.c/h` | inode/dentry/file 三级结构、路径解析、open/close/read/write |
| 设备文件系统 | [kern-devfs.md](kern-devfs.md) | `kern_devfs.c/h` | /dev/ 目录创建、设备注册 |
| /proc 与 /sys | [kern-procfs-sysfs.md](kern-procfs-sysfs.md) | `kern_procfs.c/h`, `kern_sysfs.c/h` | 内核状态信息、系统配置文件系统 |
| IPC | [kern-ipc.md](kern-ipc.md) | `kern_ipc.c/h` | 匿名 pipe（环形缓冲区）+ 命名消息队列 |
| 系统调用 | [kern-syscall.md](kern-syscall.md) | `kern_syscall.c/h` | 统一 syscall 分发器 + 用户态封装 |
| Shell | [kern-shell.md](kern-shell.md) | `kern_shell.c/h`, `kern_shell_cmds.c/h` | 串口交互式命令行（30+ 命令，含 scope/top/param 等） |
| Scope | [kern-shell.md](kern-shell.md#scope--实时数据监测引擎phase-3-新增) | `kern_shell_cmds.c`, `kern_shell_cmds_internal.h` | 实时数据监测引擎（周期 CSV 输出） |
| GPIO 桥接 | [kern-gpiofs.md](kern-gpiofs.md) | `kern_gpiofs.c/h` | /sys/gpio 引脚状态映射与读写 |
| 版本信息 | [kern-version.md](kern-version.md) | `kern_version.h` | 版本号与开发者信息集中管理 |
| 物理设备 | [kern-devices.md](kern-devices.md) | `devices/kern_devices.c`, `dev_fb0.c`, `dev_input0.c`, `dev_ttyS0.cpp` | `/dev/fb0` 帧缓冲、`/dev/input0` 按键、`/dev/ttyS0` 串口 |
| Shell 命令 | [kern-shell-cmds.md](kern-shell-cmds.md) | `kern_shell_cmds.c/h`, `kern_shell_cmds_internal.h` | 30+ 内置命令实现与动态注册 |
| Shell 解析器 | [kern-shell-parser.md](kern-shell-parser.md) | `kern_shell_parser.c/h` | 输入行解析（引号支持、参数分割） |

## 关键设计决策

### 为什么用协作式而非抢占式？

在 520KB SRAM 的 ESP32 上：
- **抢占式**需要保存完整硬件上下文（32个寄存器 + FPU + 中断状态），切换开销大
- **协作式**只需保存/恢复少量寄存器（Native 用 `ucontext`，ESP32 用 FreeRTOS 令牌协议），切换代价极低
- 无真正并发 = 无复杂锁机制 = 大幅减少代码量和运行时开销

### 为什么保留 FreeRTOS？

ESP32 Arduino 的 WiFi（`esp_wifi`）和蓝牙（NimBLE）协议栈深度依赖 FreeRTOS。Xeros 不与 FreeRTOS 竞争 —— 它运行在 Arduino `loop()` 的单一线程内，只是逻辑上的"进程层"，WiFi/BT 协议栈的 FreeRTOS 任务完全不受影响。

### 为什么 VFS 不实现全部 Linux 特性？

不实现 page cache、dentry LRU、块设备、inode 缓存。只保留核心抽象：`file_operations` 函数表（`read`/`write`/`ioctl`/`release`）。所有数据结构常驻内存，无磁盘对应的持久化需求。

## 初始化顺序

```
main.cpp setup():
    M5.begin()                    ← 硬件初始化
    hal_display_init()            ← 显示驱动
    hal_input_init()              ← 按键驱动
    storage_init()                ← NVS 存储
    settings_load_from_storage()  ← 恢复设置

    kern_init()                   ← 内核日志系统
    kern_vfs_init()               ← VFS 根节点
    kern_devfs_init()             ← /dev 目录
    kern_sched_init()             ← 调度器 + idle 任务
    kern_devices_init()           ← 注册 fb0/input0/ttyS0
    kern_sysfs_init()             ← /sys 虚拟文件
    kern_procfs_init()            ← /proc 虚拟文件

    kern_shell_init()             ← 启动 Shell 任务
    /* 后续: kern_spawn(ui_task) 等 */
```

## 文件系统布局

```
/                         ← VFS 根目录
├── dev/
│   ├── fb0               ← 帧缓冲（写命令协议）
│   ├── input0            ← 按键事件（6 字节结构化数据）
│   ├── ttyS0             ← 串口（字符设备读写）
│   └── null              ← 黑洞设备
├── proc/
│   ├── tasks             ← 任务列表（只读）
│   ├── uptime            ← 内核运行时间（只读）
│   ├── version           ← 内核版本号（只读）
│   ├── meminfo           ← 堆内存统计（只读）
│   └── developer         ← 开发者信息（只读）
├── sys/
│   ├── brightness        ← 亮度（0-255，可读写）
│   ├── rotation          ← 屏幕方向（0-3，可读写）
│   ├── anim_speed        ← 动画速度（0-100，可读写）
│   ├── anim_enabled      ← 动画开关（0/1，可读写）
│   ├── mode              ← 运行模式（0=manual/1=auto/2=calibrate/3=estop，可读写）
│   ├── ctrl              ← 控制算法（0=stop/1=start/2=reset，可读写）
│   ├── kernel/
│   │   └── log_level     ← 日志级别（0-3，可读写）
│   └── gpio/
│       ├── list          ← 所有引脚状态汇总表
│       ├── 0             ← GPIO0 状态（读写）
│       ├── 25            ← GPIO25 状态（读写）
│       ├── 26            ← GPIO26 状态（读写）
│       ├── 32            ← GPIO32 状态（读写）
│       ├── 33            ← GPIO33 状态（读写）
│       ├── 36            ← GPIO36 状态（只读）
│       └── 37            ← GPIO37 状态（只读）
└── tmp/                  ← 临时目录（可 touch/rm）
```

---

> **See Also:** [类型系统](kern-types.md) | [调度器](kern-task.md) | [VFS 核心](kern-vfs.md) | [架构计划](../../.claude/plans/microkernel.plan.md) | [Shell 扩充计划](../../.claude/plans/ui-anim-shell-kernel-v2.plan.md) | [内核优化分析](../kernel-optimization-analysis.md)
