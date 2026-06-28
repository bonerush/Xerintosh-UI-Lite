# 重构状态总览

## 当前轮次

- **开始日期**：2026-06-28
- **起始 commit**：`5d4cc00`
- **范围**：全栈（内核 → HAL → UI → App → 文档）

## 阶段状态

| 阶段 | 状态 | 开始时间 | 完成时间 | 备注 |
|------|------|----------|----------|------|
| 0 — 基线建立 | `RUNNING` | 2026-06-28 | — | 构建中 |
| 1 — 扫描诊断 | `PENDING` | — | — | |
| 2.1 — 内核重构 | `PENDING` | — | — | 调度器优化 + FreeRTOS 死代码清理 |
| 2.2 — HAL 重构 | `PENDING` | — | — | |
| 2.3 — UI 重构 | `PENDING` | — | — | 重点：100Hz-60Hz 可变刷新率 |
| 2.4 — App 重构 | `PENDING` | — | — | |
| 2.5 — 文档同步 | `PENDING` | — | — | |
| 3 — 集成验证 | `PENDING` | — | — | |
| 4 — 文档归档 | `PENDING` | — | — | |

## 本轮重点目标

1. **UI 帧率优化**：100Hz-60Hz 可变刷新率，确保 UI 丝滑显示
2. **调度器算法优化**：Xeros 内核调度器性能提升
3. **FreeRTOS 残留清理**：移除所有 FreeRTOS 死代码，纯净 Xeros API
4. **默认上传指令**：改为 `pio run -e m5stick-c-native --target upload`

## 验证规则

见 `.claude/rules/verification.md`：**先电脑验证，随后传到实机验证。**

## 历史轮次

- [2026-06-27 全栈重构](./04-archive.md) — 已完成
- [02-内核层报告](./02-refactor/kernel.md)
- [02-HAL层报告](./02-refactor/hal.md)
- [02-UI层报告](./02-refactor/ui.md)
- [02-App层报告](./02-refactor/app.md)
- [02-文档体系报告](./02-refactor/docs.md)

## 状态说明

- `PENDING`：尚未开始
- `RUNNING`：正在执行
- `DONE`：完成并通过验收
- `BLOCKED`：被阻塞，需要决策
- `REVERTED`：已回滚
