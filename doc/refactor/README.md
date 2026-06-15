# 重构跟踪：kernel-deep（2026-06-15 第二轮）

本轮重构按用户指定顺序：**内核层深度优化 → App 层对齐 → 其余层按需**。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立与冻结 | DONE（继承上轮） | coder | `00-baseline.md` |
| 1 | 深度内核诊断 | DONE（使用上轮 + App诊断） | explore | `01-diagnosis-kernel.md` |
| 2.1 | 内核层深度重构 | **DONE** | coder | `02-refactor/kernel-v2.md` |
| 2.2 | App 层重构 | **DONE** | coder | `02-refactor/app.md` |
| 2.3 | HAL 层重构 | SKIP（本轮不涉及） | — | — |
| 2.4 | UI 核心层重构 | SKIP（上轮已完成） | — | — |
| 2.5 | 文档体系重构 | **DONE** | coder | `02-refactor/app.md`, `kernel-v2.md` |
| 3 | 集成验证 | **DONE** | coder | — |
| 4 | 文档同步与归档 | **DONE** | coder | `04-archive-v2.md` |

## 本轮范围

- [x] **内核层深度**（`src/kernel/`）— P1-11、O(1)任务插入、resource节点池、设备NULL加固
- [x] **App 层**（`src/app/`）— taskmgr包装层、sm_ui getter、ui_service settings API
- [ ] **HAL 层**（`src/hal/`）— 待定
- [ ] **UI 核心层**（`src/ui/`）— 待定
- [ ] **文档体系**（`doc/`）— 待定

## 本轮内核优化明细（阶段 2.1）

| # | 项目 | 文件 | 效果 |
|---|------|------|------|
| P1-11 | 移除重复 typedef | `kern_shell_cmds.h` | 消除编译歧义风险 |
| 新增 | O(1) 任务插入 | `kern_task_lifecycle.c` + `kern_sched.c` | spawn 不再 O(n) 遍历 |
| 新增 | resource 节点池 (32) | `kern_resource.c` | 消除资源追踪 malloc |
| P1-10 | 设备 buf NULL 检查 | `dev_fb0/input0/pwrkey/ttyS0` | 加固 5 个设备驱动 |

## 本轮 App 层优化明细（阶段 2.2）

| # | 项目 | 文件 | 效果 |
|---|------|------|------|
| P1 | taskmgr 包装层 | `taskmgr.h/c` + `taskmgr_ui.c` | 消除 UI 直调内核 API |
| P2 | getter 替换 | `sm_ui.c` | 统一 settings 访问模式 |
| P2 | settings setter | `ui_service.c` | 统一 settings 写入模式 |

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 414/415 pass, 1 skipped |
| 编译警告 | ✅ 无新增 |
| RAM | 25.5% (+3.2% vs 上轮基线，因资源池 512B) |

## 快速链接

- 上轮基线：`00-baseline.md`
- 上轮诊断：`01-diagnosis-kernel.md`、`01-diagnosis-ui.md`
- 内核重构：`02-refactor/kernel.md`
- App 重构：`02-refactor/app.md`
- 命令速查：`.claude/prompt/refactor.md`
