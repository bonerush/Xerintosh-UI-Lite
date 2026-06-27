# Xeros 内核知识地图

> 本文档是 Xerintosh 项目技术文档的中央索引。
> 所有文档均使用中文撰写，代码标识符和文件路径保持原样。

## 架构概览

```
Xerintosh 项目
├── Xeros 内核 (v3.0.0)
│   ├── [原生内核架构](architecture/xeros-native-kernel.md) ← 核心设计文档
│   │   ├── [上下文切换](architecture/context-switch.md)
│   │   ├── [调度器与 Tick 定时器](architecture/scheduler.md)
│   │   ├── [IPC 任务间通信](architecture/ipc-primitives.md)
│   │   ├── [中断边界处理](architecture/interrupt-boundary.md)
│   │   └── [调试与诊断](architecture/debug-diagnostics.md)
│   ├── [实施计划](implementation-plan.md) ← 8 阶段任务分解
│   └── [迁移策略](architecture/migration-strategy.md)
├── HAL 层 (src/hal/)
│   └── *（文档待补充）*
├── 应用层 (src/app/)
│   └── [App 层文档](app/index.md)
└── UI 框架 (src/ui/)
    └── [UI 核心框架](ui/index.md)
```

## Xeros 内核特性矩阵（v3.0.0）

| 功能 | 状态 | 文档 |
|------|------|------|
| 任务通知 | 已完成 | [task-notify.md](kernel/task-notify.md) |
| 软件定时器 | 已完成 | [timer.md](kernel/timer.md) |
| ISR 安全 IPC | 已完成 | [isr-safe.md](kernel/isr-safe.md) |
| 流缓冲区 | 已完成 | [stream-buffer.md](kernel/stream-buffer.md) |
| 任务挂起/恢复/优先级 | 已完成 | [task-ctrl.md](kernel/task-ctrl.md) |
| 运行时统计/看门狗/栈溢出检测 | 已完成 | [stats.md](kernel/stats.md) |
| 临界区/中断抽象 | 已完成 | [critical.md](kernel/critical.md) |
| FreeRTOS 调度依赖 | 已移除 | [freertos-removal.md](kernel/freertos-removal.md) |

## 文档树

- **[原生内核架构](architecture/xeros-native-kernel.md)**
  - [上下文切换设计](architecture/context-switch.md) — Xtensa call0 ABI 实现
  - [调度器设计](architecture/scheduler.md) — 硬件定时器驱动的抢占式调度
  - [IPC 原语设计](architecture/ipc-primitives.md) — 信号量、互斥锁、队列、事件组
  - [中断边界处理](architecture/interrupt-boundary.md) — 延迟中断处理、嵌套支持
  - [调试与诊断](architecture/debug-diagnostics.md) — 任务检查器、调度追踪、内存分析
- **[实施计划](implementation-plan.md)** — 8 个阶段、29 个文件、详细任务分解
- **[FreeRTOS 剩余引用清单](freertos-remaining-references.md)** — 改造路线图与依赖审计
- **[原生内核调试日志](debug-xeros-native.md)** — Phase 1/2 实机调试记录与踩坑总结
- **[内核子系统](kernel/)**
  - [任务通知](kernel/task-notify.md)
  - [软件定时器](kernel/timer.md)
  - [ISR 安全 IPC](kernel/isr-safe.md)
  - [流缓冲区与消息缓冲区](kernel/stream-buffer.md)
  - [任务控制](kernel/task-ctrl.md)
  - [运行时统计与看门狗](kernel/stats.md)
  - [临界区与中断抽象](kernel/critical.md)
  - [FreeRTOS 调度依赖移除记录](kernel/freertos-removal.md)
  - [全功能适配 Debug 记录](kernel/debug/full-adaptation-debug.md)
  - [可移植层](kernel/port.md)
  - [VFS Dentry-Tree 并发保护](kernel/vfs-concurrency.md)
- **[UI 核心框架](ui/index.md)** — 菜单树、输入分派、动画与渲染
- **[App 层](app/index.md)** — 菜单构建、user_item 契约、设置转换
- **[API 模板教程](tutorials/api-templates.md)** — 常用 API 模板与常见陷阱
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

- **FreeRTOS 隔离**: 兼容层已移除；原生调度器路径下已无显式 FreeRTOS 调度 API 调用（仅 ESP-IDF 驱动内部仍使用 FreeRTOS）
- **可插拔后端**: 通过 `kern_port_ops_t` 操作表实现后端多态；`kern_port_esp32_native.c` 已替代 FreeRTOS 后端成为 `m5stick-c-native` 实际后端
- **已有 IPC**: `xeros_spinlock_t`、递归 mutex（基于 `xeros_spinlock_t`）
- **已有 SMP**: per-CPU 数据结构、核心亲和性、负载均衡；已在 `m5stick-c-native` 原生调度器下启用双核调度
- **tickless idle**: GPTimer 动态重编程已实现，空闲时按下一个唤醒事件睡眠而非固定 1ms tick
- **WiFi**: 在原生调度器下已启用 `wifi-mgr` 任务，ESP-IDF WiFi 驱动内部 FreeRTOS 任务与 Xeros 原生任务共存
- **IPC/SMP 安全**: 已给所有 IPC 原语加 SMP 自旋锁，修复 tickless idle 遍历任务链表的原子性、修复 CPU 分配的原子性；**IPI 已实现**，IPC 唤醒与任务创建时会通知远程核心重新调度
- **VFS 并发**: 已添加全局 `g_vfs_lock` 自旋锁保护 dentry 树与 inode 引用计数，`kern_open`/`kern_close` 等路径已加锁
- **调度器状态修复**: 已修复 `kern_sched_tick` 未设置 RUNNING 状态、`kern_sleep_ms` yield 覆盖 SLEEPING、GPTimer tick 未启动等 bug
- **调试工具完善**: `xeros_debug.py` 启动检测、复位序列、正则匹配已修复，支持 `--reset --wait-boot --cmd ps` 端到端自动化
- **UI 核心重构完成**: 空指针/空列表保护、布局常量提取、`xerintosh_draw_list_item` 拆分、选择器速度复位集中化、退场动画魔法数字常量化、弹窗换行 `sizeof` 保护、脏矩形升级到区域追踪
- **App 层重构完成**: `app_menu.c` 拆分为 `app_menu_core.c` + `app_menu_entries.c`；`user_item_contract.h` 显式化生命周期契约；`settings_level_to_hw` / `settings_hw_to_level` 集中设置项转换；`power_key_popup` 补齐 native 测试
- **文档体系重构完成**: `doc/` 已新增 `ui/`、`app/`、`tutorials/` 索引，修复 `ui_item_core.h` 中断链引用
- **Xeros 内核底层全功能适配完成**: 任务通知、软件定时器、ISR 安全 IPC、流缓冲区、任务控制、运行时统计/看门狗/栈溢出检测、临界区抽象已实现；PI 互斥锁恢复基线优先级、事件组高 8 位保留、FIFO 调度器优先级桶优化、核心启动桩隔离、`vTaskDelay` 移除均已完成并通过 native/硬件构建验证
- **目标**: 继续验证 UI/WiFi 实际屏幕操作（调度器层面已稳定，需用户现场确认显示渲染与 WiFi 菜单交互）

## 重构记录

| 日期 | 范围 | 状态 | 报告 |
|---|---|---|---|
| 2026-06-27 | Xeros 内核 / HAL / UI / App / 文档 | 阶段 2 完成 | [refactor/README.md](refactor/README.md) |
| 2026-06-27 | Xeros 内核底层全功能适配 | 完成 | [kernel/debug/full-adaptation-debug.md](kernel/debug/full-adaptation-debug.md) |

## 相关源文件

| 源文件 | 描述 |
|--------|------|
| `src/kernel/kern_port.h` | 可移植层操作表定义 |
| `src/kernel/kern_port_freertos.c` | FreeRTOS 后端（默认 `m5stick-c` 环境使用） |
| `src/kernel/kern_port_esp32_native.c` | ESP32 原生调度器后端（`m5stick-c-native` 环境使用） |
| `src/kernel/kern_task.h` | 任务控制块（TCB）定义 |
| `src/kernel/kern_sched.h` | 调度器内部接口 |
| `src/kernel/kern_smp.h` | SMP 多核支持 |
| `src/kernel/kern_sync.h` | 同步原语（spinlock、mutex） |
| `src/kernel/kern_types.h` | 基础类型和错误码 |
| `src/kernel/kern_kmalloc.c` | 内核内存分配器 |

---

> **See Also:** [实施计划](implementation-plan.md) | [原生内核架构](architecture/xeros-native-kernel.md)
