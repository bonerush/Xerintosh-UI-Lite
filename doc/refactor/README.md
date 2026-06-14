# M5Stick-P1 重构跟踪：内核 + UI 重点优化

> **范围**：Xeros 内核层、Xerintosh UI 核心层（重点），HAL / App / 文档同步。
> **分支**：`refactor/2026-06-14-kernel-ui`
> **起始 commit**：`6411c578124f1bf029169ffe1f5dfbef9fddeee5`
> **日期**：2026-06-14

---

## 阶段状态

| 阶段 | 状态 | 产物 |
|------|------|------|
| 0. 基线建立 | `DONE` | `00-baseline.md` |
| 1. 扫描诊断 | `DONE` | `01-diagnosis.md` |
| 2.1 内核层重构 | `DONE` | `02-refactor/kernel.md` |
| 2.2 HAL 层重构 | `SKIPPED` | `02-refactor/hal.md` |
| 2.3 UI 核心层重构 | `DONE` | `02-refactor/ui.md` |
| 2.4 App 层重构 | `DONE` | `02-refactor/app.md` |
| 2.5 文档同步 | `PENDING` | `02-refactor/docs.md` |
| 3. 集成验证 | `DONE` | `03-integration.md` |
| 4. 归档 | `PENDING` | `04-archive.md` |

---

## 重点方向

1. **内核层**：统一设备注册入口、统一错误码、强化任务退出资源回收、明确调度类语义。
2. **UI 核心层**：消灭 item 类型处理的内联 switch、封装动画插值公式、增强边界条件测试。

---

## 约束

- 一次只动一层。
- 先加测试再改代码。
- public API 变化必须同步 `doc/`。
- 禁止 force push。
