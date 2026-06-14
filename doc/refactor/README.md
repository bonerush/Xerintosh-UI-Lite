# 重构跟踪：kernel-first（2026-06-14）

本轮重构聚焦 **Xeros 内核层** 优先，逐步辐射 HAL、UI、App 与文档体系。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立与冻结 | DONE | coder | `00-baseline.md` |
| 1 | 扫描与诊断 | DONE | explore | `01-diagnosis.md` |
| 2.1 | 内核层重构 | DONE | coder | `02-refactor/kernel.md` |
| 2.2 | App 上层重构 | DONE | coder | `02-refactor/app.md` |
| 2.3 | HAL 层重构 | DONE | coder | `02-refactor/hal.md` |
| 2.4 | UI 核心层重构 | DONE | coder | `02-refactor/ui.md` |
| 2.5 | 文档体系同步 | RUNNING | coder | `02-refactor/docs.md` |
| 3 | 集成验证 | PENDING | verification | `03-integration.md` |
| 4 | 文档同步与归档 | PENDING | coder | `04-archive.md` |

## 本轮范围

- [x] 内核层（`src/kernel/`）
- [ ] App 层（`src/app/`）
- [ ] HAL 层（`src/hal/`）
- [ ] UI 核心层（`src/ui/`）
- [ ] 文档体系（`doc/`）

## 快速链接

- 基线报告：`00-baseline.md`
- 重构范围：`02-refactor/kernel.md`（待创建）
- 命令速查：`.claude/prompt/refactor.md`
