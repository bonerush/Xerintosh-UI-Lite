# Xeros 内核知识地图

> 本文档是 Xerintosh 项目技术文档的中央索引。
> 所有文档均使用中文撰写，代码标识符和文件路径保持原样。

## 架构概览

```
Xerintosh 项目
├── Xeros 内核 (v2.4.0)
│   ├── [原生内核架构](architecture/xeros-native-kernel.md) ← 核心设计文档
│   │   ├── [上下文切换](architecture/context-switch.md)
│   │   ├── [调度器与 Tick 定时器](architecture/scheduler.md)
│   │   ├── [IPC 任务间通信](architecture/ipc-primitives.md)
│   │   ├── [中断边界处理](architecture/interrupt-boundary.md)
│   │   └── [调试与诊断](architecture/debug-diagnostics.md)
│   ├── [实施计划](implementation-plan.md) ← 8 阶段任务分解
│   └── [迁移策略](architecture/migration-strategy.md)
├── HAL 层 (src/hal/)
├── 应用层 (src/app/)
└── UI 框架 (src/ui/)
```

## 文档树

- **[原生内核架构](architecture/xeros-native-kernel.md)**
  - [上下文切换设计](architecture/context-switch.md) — Xtensa call0 ABI 实现
  - [调度器设计](architecture/scheduler.md) — 硬件定时器驱动的抢占式调度
  - [IPC 原语设计](architecture/ipc-primitives.md) — 信号量、互斥锁、队列、事件组
  - [中断边界处理](architecture/interrupt-boundary.md) — 延迟中断处理、嵌套支持
  - [调试与诊断](architecture/debug-diagnostics.md) — 任务检查器、调度追踪、内存分析
- **[实施计划](implementation-plan.md)** — 8 个阶段、29 个文件、详细任务分解
- **[FreeRTOS 剩余引用清单](freertos-remaining-references.md)** — 改造路线图与依赖审计
- **[参考资料](reference/index.md)** — FreeRTOS Xtensa port 源码与 Xtensa ABI 文档

## 快速链接

| 文档 | 描述 | 关键内容 |
|------|------|----------|
| [原生内核架构](architecture/xeros-native-kernel.md) | 整体架构设计 | 组件图、设计原则、API 设计 |
| [上下文切换](architecture/context-switch.md) | 最底层的上下文切换 | Xtensa 汇编、寄存器保存/恢复 |
| [调度器](architecture/scheduler.md) | 调度器和定时器 | 抢占式调度、SMP、tick 管理 |
| [IPC 原语](architecture/ipc-primitives.md) | 任务间通信 | 信号量、互斥锁、队列、事件组 |
| [实施计划](implementation-plan.md) | 任务分解和时间线 | 8 阶段、风险评估、测试策略 |
| [FreeRTOS 剩余引用清单](freertos-remaining-references.md) | 依赖审计 | 剩余引用分类与改造建议 |

## 当前项目状态

- **FreeRTOS 隔离**: 兼容层已移除；显式 FreeRTOS 调用仅剩调度器宿主喂狗与启动阶段延时 fallback
- **可插拔后端**: 通过 `kern_port_ops_t` 操作表实现后端多态
- **已有 IPC**: `xeros_spinlock_t`、递归 mutex（基于 `xeros_spinlock_t`）
- **已有 SMP**: per-CPU 数据结构、核心亲和性、负载均衡
- **目标**: 进一步移除显式 FreeRTOS API，最终实现完全自主的原生后端

## 相关源文件

| 源文件 | 描述 |
|--------|------|
| `src/kernel/kern_port.h` | 可移植层操作表定义 |
| `src/kernel/kern_port_freertos.c` | FreeRTOS 后端（当前唯一生产后端） |
| `src/kernel/kern_task.h` | 任务控制块（TCB）定义 |
| `src/kernel/kern_sched.h` | 调度器内部接口 |
| `src/kernel/kern_smp.h` | SMP 多核支持 |
| `src/kernel/kern_sync.h` | 同步原语（spinlock、mutex） |
| `src/kernel/kern_types.h` | 基础类型和错误码 |
| `src/kernel/kern_kmalloc.c` | 内核内存分配器 |

---

> **See Also:** [实施计划](implementation-plan.md) | [原生内核架构](architecture/xeros-native-kernel.md)
