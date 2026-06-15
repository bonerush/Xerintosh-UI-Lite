# 归档报告（2026-06-15 kernel-ui-performance）

## 本轮总结

M5Stick-P1 第二轮重构：聚焦内核性能（内存/CPU/实时性）和 UI 流畅度（动画/帧率/响应）。

## 达成目标

| 维度 | 目标 | 达成方式 | 状态 |
|------|------|----------|------|
| **内核 CPU** | 减少调度器开销 | O(1) tail 指针 enqueue | ✅ |
| **内核内存** | 减少堆碎片 | FD 对象池消除 kern_open malloc | ✅ |
| **UI 帧率** | 静态画面零重绘 | 脏矩形帧跳过机制 | ✅ |
| **UI 动画** | 选择器绘制加速 | XOR 批量操作（15次→1次） | ✅ |
| **UI 响应** | 减少导航开销 | 装饰缓存 + 解引用缓存 | ✅ |

## 变更统计

| 类别 | 文件数 | +行 | -行 |
|------|--------|-----|-----|
| 内核层 | 5 | +88 | -22 |
| UI 层 | 7 | +88 | -25 |
| 文档 | 6 | +631 | -308 |
| **合计** | **18** | **+807** | **-355** |

## 关键决策

1. **FD 对象池 vs slab 分配器**：选择简单的固定大小位图池（16 项），非通用 slab，复杂度低且满足当前需求。
2. **脏矩形 vs 增量渲染**：选择简单的 dirty 标志而非完整脏矩形，因为 80×160 小屏全帧重绘开销可控。
3. **XOR 缓冲区大小**：静态分配 9600 字节（160×30），可覆盖最大选择器。若未来屏幕更大需调整。
4. **tail 指针防御**：enqueue 中检测 NULL tail 并回退 O(n)，作为安全网而非完全依赖 init 正确性。

## 文件变更清单

```
src/kernel/kern_sched_class.h   — task_list_tail 字段
src/kernel/kern_sched_rr.c      — O(1) enqueue + dequeue tail 维护
src/kernel/kern_sched_fifo.c    — FIFO dequeue tail 维护
src/kernel/kern_sched.c         — 三处 init tail 同步
src/kernel/kern_vfs.c           — FD 对象池（g_fd_pool[16]）
src/ui/ui_context.h             — dirty 字段
src/ui/ui_context.c             — dirty 初始化
src/ui/ui_core.c                — 脏矩形跳过 + 动画 dirty 标记 + 内部二次检查
src/app/ui_task.c               — 主循环条件跳过
src/ui/ui_dispatch.c            — 输入处理 dirty 设置
src/ui/ui_item_selector.c       — dirty 设置 + 解引用缓存
src/hal/hal_display_adv.cpp     — XOR 批量操作
src/ui/ui_draw_list.c           — 装饰缓存
doc/refactor/00-baseline.md     — 基线报告
doc/refactor/01-diagnosis.md    — 诊断报告
doc/refactor/02-refactor/kernel.md — 内核重构报告
doc/refactor/02-refactor/ui.md  — UI 重构报告
doc/refactor/03-integration.md  — 集成验证报告
doc/refactor/README.md          — 状态跟踪
```

## 后续建议

1. **P0 ISR 安全 bug**（内核 agent 发现的硬件定时器 ISR 中调用 `xSemaphoreGive/Take` 和 `free()`）— 需单独处理，影响 ESP32 硬件运行稳定性。
2. **K02 pick_next 双遍扫描优化**— 独立 wake_list 可进一步减少调度开销。
3. **Flash 使用率** 88.1% — 添加新功能前需考虑。
4. 如需进一步 UI 优化，可考虑：字体渲染缓存、M5GFX 原生 XOR 模式、帧率目标控制。
