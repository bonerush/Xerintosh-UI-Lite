# 重构跟踪：shell-wifi-kernel（第十一轮 · 2026-06-19 · shell/WiFi/内核管线修复与代码优化）

> **当前状态**：✅ 阶段 2.4 完成
> **当前分支**: `refactor/2026-06-19-shell-wifi-kernel`
> **本轮目标**：基于第十轮修复的 shell 和 WiFi，深入检查对应内核管线、简化重复代码、统一代码风格、原子化更新文档

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-shell-wifi-kernel.md` |
| 1 | 扫描诊断 | **DONE** | explore | `01-diagnosis-shell-wifi-kernel.md` |
| 2.1 | 内核层重构 | **DONE** | coder | `02-refactor/kernel.md` |
| 2.2 | HAL 层重构 | **DONE** | coder | `02-refactor/hal.md` |
| 2.3 | UI 核心层 | **DONE** | coder | `02-refactor/ui.md` |
| 2.4 | App 层重构 | **DONE** | coder | `02-refactor/app.md` |
| 2.5 | 文档体系 | **PENDING** | coder | `02-refactor/docs.md` |
| 3 | 集成验证 | **PENDING** | verification | `03-integration.md` |
| 4 | 归档 | **PENDING** | coder | `04-archive.md` |

## 本轮重点关注

| 领域 | 关注项 |
|------|--------|
| Shell | 第十轮修复的 Tab 补全、相对路径、cat 设备文件、退格删提示符等是否还有边界问题 |
| WiFi | 第十轮修复的 popup 保护、网络菜单出现逻辑是否还有竞态或简化空间 |
| 内核管线 | ttyS0、dev_input0、dev_pwrkey、VFS、资源释放与 shell/WiFi 的交互路径 |
| 代码简化 | 重复的状态机、转换表、错误处理是否可抽取统一 |
| 代码风格 | 模块前缀、extern "C"、include guard、回调签名一致性 |
| 文档 | 原子化更新，每个 public API 变化同步更新 `doc/`，并附源链接 |

## 上一轮记录（第十轮 · fullstack）

> ✅ 已合并到 `main`。详细见 `04-archive.md`（第十轮）。
