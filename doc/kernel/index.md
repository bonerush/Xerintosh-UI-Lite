# Xeros 内核文档

本目录包含 Xeros 内核各子系统的技术文档。

## 文档列表

- [任务通知](task-notify.md) — 轻量级 per-task 同步
- [软件定时器](timer.md) — 命令队列 + 守护任务
- [ISR 安全 IPC](isr-safe.md) — 中断上下文可用的 IPC 原语
- [流缓冲区与消息缓冲区](stream-buffer.md) — 字节流与带长度前缀的消息传输
- [任务控制](task-ctrl.md) — 挂起/恢复/优先级/延迟
- [运行时统计与看门狗](stats.md) — CPU 统计、看门狗、栈溢出检测
- [临界区与中断抽象](critical.md) — Xtensa PS.INTLEVEL 临界区
- [FreeRTOS 调度依赖移除记录](freertos-removal.md) — V2 去 FreeRTOS 调度器依赖说明
- [全功能适配 Debug 记录](debug/full-adaptation-debug.md) — Commit 清单与关键问题排查
- [可移植层（Port Layer）](port.md) — `kern_port_ops_t` 操作表与定时器基础设施
- [VFS Dentry-Tree 并发保护](vfs-concurrency.md) — 全局自旋锁保护 dentry 树与 inode 引用计数

---

> **Parent:** [项目知识地图](../index.md)
