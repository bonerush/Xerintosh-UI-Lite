# 诊断报告：docs-architecture-diagrams（2026-06-15 第四轮）

## 扫描范围
全量 `doc/` 目录（38 个 .md 文件，排除 `doc/refactor/`）vs `src/` 目录源码。

## 发现摘要

### 诊断维度 1：文档与源码同步 — 15 个准确性问题

| 等级 | 数量 | 典型问题 |
|------|------|----------|
| **HIGH** | 3 | `doc/ui/core.md` API 表错误列出 3 个 `static` 内部函数 |
| **MEDIUM** | 5 | `dirty` 字段遗漏、`kern_sched` 漏列、NimBLE vs Classic BT 描述冲突 |
| **LOW** | 7 | 文件引用遗漏、重复条目、行号偏移 |
| ✓ 验证项 | 8 | 核心 API 与数据结构描述准确 |

### 诊断维度 2：未文档化模块 — 16 个

| 层级 | 覆盖率 | 最优先级缺口 |
|------|--------|-------------|
| Kernel | 87% (27/31) | `debug_serial`, `kern_ctx_esp32`, `dev_pwrkey`, `kern_sched` |
| UI Core | 91% (10/11) | `ui_dirty.h` — 重构产出无独立文档 |
| HAL | 57% (4/7) | `hal_power_key`, `hal_layout`, `hal_input_double_click` |
| App | 60% (12/20) | `wifi/`, `bluetooth/`, `shutdown/` 无独立文档 |

### 诊断维度 3：架构图缺失 — 7 个

| 图 | 状态 | 备注 |
|---|---|---|
| 系统总体架构图 | ❌ 缺 Mermaid | 仅有 PNG drawio |
| 系统启动流程图 | ❌ 缺 | 无图 |
| Xeros 内核结构图 | ❌ 缺 Mermaid | 仅有 ASCII box art |
| VFS 与设备桥接图 | ❌ 缺 Mermaid | 仅有 ASCII box art |
| UI 渲染管线图 | ❌ 缺 Mermaid | 仅有文字列表 |
| user_item 生命周期图 | ❌ 缺 Mermaid | 仅有 ASCII 文字流程 |
| 典型 App 功能图 | ❌ 缺 | 无图 |

## 修正计划

### 阶段 2(docs) 实施范围

1. **添加 7 个 Mermaid 架构图**（优先级 P0）
2. **修正 3 个 HIGH 问题**（移除虚假公开 API，P0）
3. **修正 5 个 MEDIUM 问题**（补充字段/模块/修正描述，P1）
4. **修正 5 个 LOW 问题**（补充遗漏条目，P1）
5. **新增 `ui_dirty.md` 文档**（补漏，P1）

### 不纳入本轮修正（P2 / 超出范围）

以下 LOW 问题待下一轮处理：
- 行号偏移（3 处）— 不影响可读性
- `KERN_EPERM` 遗漏（1 处）— 基本错误码
- 未文档化的复杂子模块（wifi/bluetooth/shutdown）— 需单独轮次，需源码分析

## 验收标准

- [x] 新增 7 个 Mermaid 图位置正确、内容准确
- [x] HIGH 问题全部修复（3/3）
- [x] MEDIUM 问题全部修复（5/5）
- [x] LOW 问题核心项修复（5/7，跳过低优先项）
- [x] `pio run -e m5stick-c` 通过
- [x] `pio test -e native` 全量通过
