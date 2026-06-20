# 重构跟踪：memory-schedule（第十二轮 · 2026-06-19 · 内核内存分配与调度按需分配）

> **当前状态**：ARCHIVED
> **当前分支**: `refactor/2026-06-19-memory-schedule`
> **本轮目标**：改进 Xeros 内核的内存分配与任务调度机制，使任务栈/内存按当前实际需求自动分配，解决 WiFi/蓝牙因静态内存预留导致无法共存和服务错误的问题。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-memory-schedule.md` |
| 1 | 扫描诊断 | **DONE** | explore | `01-diagnosis-memory-schedule.md` |
| 2.1 | 内核层内存分配重构 | **DONE** | plan → coder → verification | `02-refactor/kernel-memory.md` |
| 2.2 | 内核层调度机制重构 | **DONE** | plan → coder → verification | `02-refactor/kernel-schedule.md` |
| 3 | 集成验证 | **DONE** | verification | `03-integration-memory-schedule.md` |
| 4 | 归档 | **DONE** | coder | `04-archive-memory-schedule.md` |

## 本轮重点关注

| 领域 | 关注项 |
|------|--------|
| 内存分配 | `kern_task_stack.c` 静态栈预留 vs 动态按需分配；kmalloc/kfree 配对与碎片 |
| 调度机制 | 调度类是否支持按任务实际优先级/资源需求动态调整时间片 |
| WiFi/蓝牙共存 | 静态大buffer是否导致堆耗尽；服务初始化顺序与内存峰值 |
| 回归保护 | native 任务/栈/kmalloc 测试；硬件构建无新增警告 |
| 文档 | `doc/kernel/` 中任务、调度、内存相关文档同步更新 |

## 上一轮记录（第十一轮 · shell-wifi-kernel）

> 阶段 3/4 待收尾。详细见 `03-integration-shell-wifi-kernel.md` / `04-archive-shell-wifi-kernel.md`。
