# 重构跟踪：ui-dirty-rect（2026-06-15 第三轮）

本轮重构聚焦：**UI 渲染管线的局部刷新与脏矩形处理统一接口**。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立与冻结 | **DONE** | coder | (继承上轮基线) |
| 1 | UI 渲染管线诊断 | **DONE** | coder | `01-diagnosis-ui-render.md`（内联分析） |
| 2.3 | UI 核心层重构 | **DONE** | coder | `02-refactor/ui-dirty.md` |
| 3 | 集成验证 | **DONE** | coder | 构建 + native 测试通过 |
| 4 | 文档同步与归档 | **DONE** | coder | `developer-guide.md` 已更新 |

## 本轮范围

- [x] **UI 核心层**（`src/ui/`）— 脏矩形 API 统一化：新建 `ui_dirty.h/c`，标准化 `xerintosh_invalidate()` 接口
- [x] **App 层对齐**（`src/app/`）— `ui_task.c` 迁移至新 API
- [x] **文档更新**（`doc/`）— `developer-guide.md` 新增脏矩形 API 参考表

## 核心改动

| # | 项目 | 文件 | 效果 |
|---|------|------|------|
| 新建 | 脏矩形管理模块 | `src/ui/ui_dirty.h/c` | 统一入口，三元 API |
| 替换 | 22 处直接写入 | 7 个文件 | 全部走 `xerintosh_invalidate()` |
| 替换 | 2 处直接读取 | `ui_core.c` + `ui_task.c` | 走 `xerintosh_is_dirty()` |
| 文档 | API 参考表 | `developer-guide.md` | 开发者可清晰知晓何时/如何标记脏 |

## 新 API 速查

| 函数 | 对象 | 说明 |
|------|------|------|
| `xerintosh_invalidate()` | **App 开发者** | 标记 UI 需要重绘 |
| `xerintosh_is_dirty()` | 框架内部 | 查询脏状态 |
| `xerintosh_clear_dirty()` | 框架内部 | 清除脏标志 |
| `xerintosh_mark_dirty()` | 向后兼容 | `xerintosh_invalidate()` 别名 |

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 414/415 pass, 1 skipped |
| 编译警告 | ✅ 无新增 |
| RAM | 25.5%（无变化） |

## 快速链接

- 重构报告：`02-refactor/ui-dirty.md`
- 新模块源码：`src/ui/ui_dirty.h`、`src/ui/ui_dirty.c`
- 开发者文档：`doc/developer-guide.md` §8.7
- 上轮内核重构：`02-refactor/kernel-v2.md`
