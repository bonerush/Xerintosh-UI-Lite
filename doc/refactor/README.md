# Xerintosh 全栈重构跟踪

## 当前轮次

- **分支**：`refactor/2026-06-27-fullstack`
- **起始 commit**：`7ac97ef`
- **目标**：对 Xeros 内核、HAL、UI 核心、App、文档体系进行原子级重构与优化。

## 阶段状态

| 阶段 | 状态 | 说明 |
|------|------|------|
| 0 基线建立 | DONE | 隔离 worktree 已创建，构建与 native 测试通过 |
| 1 扫描诊断 | DONE | 诊断报告已生成 |
| 2 分层重构 | DONE | kernel / HAL / UI / App / 文档体系均已完成 |
| 3 集成验证 | RUNNING | 待执行全量 native 测试、硬件构建与冒烟测试 |
| 4 文档归档 | PENDING | 待启动 |

### 分层进度

| 层级 | 状态 | 报告 |
|------|------|------|
| kernel | DONE | [kernel.md](02-refactor/kernel.md) |
| hal | DONE | [hal.md](02-refactor/hal.md) |
| ui | DONE | [ui.md](02-refactor/ui.md) |
| app | DONE | [app.md](02-refactor/app.md) |
| docs | DONE | [docs.md](02-refactor/docs.md) |

## 状态说明

- `PENDING`：尚未开始
- `RUNNING`：正在执行
- `DONE`：完成并通过验收
- `BLOCKED`：被阻塞，需要决策
- `REVERTED`：已回滚

## 报告索引

- [00-基线报告](00-baseline.md)
- [01-诊断报告](01-diagnosis.md)
- 02-分层重构报告
  - [kernel.md](02-refactor/kernel.md)
  - [hal.md](02-refactor/hal.md)
  - [ui.md](02-refactor/ui.md)
  - [app.md](02-refactor/app.md)
  - [docs.md](02-refactor/docs.md)
- [03-集成验证报告](03-integration.md)
- [04-归档报告](04-archive.md)

## 关键约束

1. 一次只动一层。
2. 先加测试再改代码。
3. public API 变化必须同步 `doc/`。
4. 每阶段结束必须可 `git revert`。
5. 关注 ESP32-PICO 520KB SRAM 限制。

---

> **Parent:** [doc/index.md](../../index.md)
