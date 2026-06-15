# 重构基线报告：docs-architecture-diagrams（2026-06-15 第四轮）

## 分支与 Commit
- 分支：`refactor/2026-06-15-docs-architecture-diagrams`
- 起始 commit：`3ef97871bf1976c7333a10d0ebf88e47e756c3b7`
- 基于：`main` (fast-forward 自 refactor/2026-06-15-ui-dirty-rect)

## 构建基线
- `pio run -e m5stick-c`：✅ PASS（RAM 25.5%, Flash 88.1%）
- `pio test -e native`：✅ PASS（414/415 pass, 1 skipped）

## 代码规模
- `src/`：~90 个 C/C++ 源文件 + 26 个头文件
- `doc/`：38 个 .md 文件（含 kernel 26 + ui 11 + hal 4 + app 8 + 教程 2 + 顶层 4）

## 现有架构图
| 图名 | .drawio | .png | 位置 |
|------|---------|------|------|
| kernel-v2-architecture | ✅ | ✅ | `doc/assets/diagrams/` |
| task-lifecycle | ✅ | ✅ | `doc/assets/diagrams/` |
| vfs-bridge | ✅ | ✅ | `doc/assets/diagrams/` |
| smp-architecture | ✅ | ✅ | `doc/assets/diagrams/` |
| sched-class-chain | ✅ | ✅ | `doc/assets/diagrams/` |
| resource-tracking | ✅ | ✅ | `doc/assets/diagrams/` |
| kmalloc-layout | ✅ | ✅ | `doc/assets/diagrams/` |

## 本次重构范围
- [x] 文档体系（docs）
- 目标：添加 7 个 Mermaid 架构图 + 修正 15 个文档准确性问题
