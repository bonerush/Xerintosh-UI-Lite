# 重构跟踪：app-transition-device-optimizations（第八轮 · 2026-06-16）

本轮重构聚焦：
- **App 进入/退出过渡动画**：为所有 `user_item` App 添加进入/退出 slide/fade 动画，例如任务管理器进入时任务项向上滑入的连贯感。
- **App 系统 API 调用审计**：检查 App 是否存在非标准 API 调用或绕过 Xeros 内核直接调用底层 API，如有则修复。
- **GPIO 设备优化 + 其余设备优化**：在内核设备层优化 GPIO 设备（`kern_gpiofs.c`）及其余设备（`dev_fb0.c`、`dev_input0.c`、`dev_ttyS0.cpp`、`dev_pwrkey.c`）。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-app-transition-device-optimizations.md` |
| 1 | 扫描诊断 | **DONE** | explore | `01-diagnosis-app-transition-device-optimizations.md` |
| 2.1 | 内核层设备优化 | **PENDING** | coder | `02-refactor/kernel-device-optimizations.md` |
| 2.3 | UI 核心层过渡动画基础设施 | **PENDING** | coder | `02-refactor/ui-transition-infrastructure.md` |
| 2.4 | App 层过渡动画 + API 调用修复 | **PENDING** | coder | `02-refactor/app-transition-api-audit.md` |
| 2.5 | 文档体系 | **PENDING** | coder | `02-refactor/docs.md` |
| 3 | 集成验证 | **PENDING** | verification | `03-integration-app-transition-device-optimizations.md` |
| 4 | 归档 | **PENDING** | coder | `04-archive-app-transition-device-optimizations.md` |

---
