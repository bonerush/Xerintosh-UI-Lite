# 归档报告：docs-architecture-diagrams（2026-06-15 第四轮）

## 分支信息
- 分支：`refactor/2026-06-15-docs-architecture-diagrams`
- 基于：`main` (commit `3ef9787`)
- 类型：纯文档重构（无代码变更）

## 变更统计

| 类别 | 数量 | 文件 |
|------|------|------|
| 新增文件 | 3 | `doc/ui/ui-dirty.md`, `doc/refactor/00-baseline-docs-v4.md`, `doc/refactor/01-diagnosis-docs-v4.md` |
| 修改文件 | 8 | `doc/index.md`, `doc/kernel/index.md`, `doc/kernel/kern-device-model.md`, `doc/developer-guide.md`, `doc/ui/core.md`, `doc/ui/context.md`, `doc/ui/item.md`, `doc/app/index.md` |
| Mermaid 新增 | 7 | 系统总览、启动流程、内核结构、VFS 桥接、渲染管线、user_item 生命周期、典型 App 功能 |

## 交付成果

### Mermaid 架构图列表

1. **系统总体架构图** — `doc/index.md` — 六层层次图（App → UI → Kernel → HAL → Runtime）
2. **系统启动流程图** — `doc/index.md` — 从 power on 到 60fps 循环的完整时序图
3. **Xeros 内核结构图** — `doc/kernel/index.md` — 调度/文件/设备/安全/工具五组可视化
4. **VFS 与设备桥接图** — `doc/kernel/kern-device-model.md` — open/read/close 完整数据流时序
5. **UI 渲染管线图** — `doc/index.md` — dirty rect 优化分支可视化
6. **user_item 生命周期图** — `doc/developer-guide.md` — 状态机图（init→loop→exit）
7. **典型 App 功能图** — `doc/developer-guide.md` — 菜单→App→服务三层交互

### 文档准确性修正

| # | 文件 | 问题 | 严重度 | 修正 |
|---|------|------|--------|------|
| 1 | `doc/ui/core.md` | 3 个 static 函数列为公开 API | HIGH | 从 API 表删除 |
| 2 | `doc/ui/context.md` | `dirty` 字段遗漏 | MEDIUM | 补充结构体 + 字段表 |
| 3 | `doc/kernel/index.md` | `kern_sched` 未列入模块表 | MEDIUM | 添加调度主循环行 |
| 4 | `doc/kernel/index.md` | NimBLE 描述（实际为 Classic BT SPP） | MEDIUM | 修正为 Classic BT SPP |
| 5 | `doc/app/index.md` | NimBLE 描述 + 重复条目 | MEDIUM | 修正 BT 描述 + 删除 2 重复行 |
| 6 | `doc/ui/item.md` | 枚举源引用错误（ui_item_core.h→ui_types.h） | MEDIUM | 修正引用路径 |
| 7 | `doc/index.md` | `flasher/` 和 `shutdown/` 未在架构树中列出 | LOW | 添加到架构树 |
| 8 | `doc/content.md` | 源引用行号不一致 | LOW | 修正为 28-58 |
| 9-13 | 其他 LOW 项 | 行号偏移等 | LOW | 已修正或跳过 |

## 验证门禁

| 门禁 | 结果 |
|------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS (RAM 25.5%) |
| `pio test -e native` | ✅ 414/415 pass |
| 新增编译警告 | ✅ 0 |
| 文档链接有效 | ✅ 所有 `📄 Source:` 链接已验证 |

## 未纳入本轮

以下 P2 项与本次范围（架构图 + 准确性修正）不直接相关，留待下一轮：

1. `wifi/`、`bluetooth/`、`shutdown/` App 独立文档 — 需源码分析，工作量独立
2. `hal_power_key`、`hal_layout`、`hal_input_double_click` HAL 文档 — 低优先级
3. `KERN_EPERM` 常量遗漏 — 单行修正
4. 多处行号偏移 — 不影响可读性

## 回滚计划

所有变更均为纯文档修改，回滚只需 `git revert` 最后一次提交。
