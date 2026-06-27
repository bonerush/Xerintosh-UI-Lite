# 归档报告

> **Parent:** [doc/refactor/README.md](README.md)

## 重构概览

| 项目 | 内容 |
|---|---|
| 分支 | `refactor/2026-06-27-fullstack` |
| 起始 commit | `7ac97ef` |
| 结束 commit | `c0c71ba` |
| 范围 | Xeros 内核 / HAL / UI 核心 / App / 文档体系 |
| 目标 | 原子级重构与优化，逐层消除技术债务，提升可测试性与可维护性 |

## 各阶段报告

| 阶段 | 报告 | 状态 |
|---|---|---|
| 0 基线建立 | [00-baseline.md](00-baseline.md) | DONE |
| 1 扫描诊断 | [01-diagnosis.md](01-diagnosis.md) | DONE |
| 2 分层重构 | [02-refactor/kernel.md](02-refactor/kernel.md) | DONE |
| | [02-refactor/hal.md](02-refactor/hal.md) | DONE |
| | [02-refactor/ui.md](02-refactor/ui.md) | DONE |
| | [02-refactor/app.md](02-refactor/app.md) | DONE |
| | [02-refactor/docs.md](02-refactor/docs.md) | DONE |
| 3 集成验证 | [03-integration.md](03-integration.md) | DONE（软件层面） |
| 4 归档 | [04-archive.md](04-archive.md) | DONE |

## 关键变更摘要

### Xeros 内核层

- 统一移植层 timer API 返回类型为 `kern_err_t`
- 封装调度器全局状态访问器
- 提取调度器初始化与任务创建公共逻辑
- 补充 [Port Layer 文档](../kernel/port.md)

### HAL 层

- 完成 HAL 层重构（详见 [hal.md](02-refactor/hal.md)）

### UI 核心层

- 空指针与空列表保护
- 布局常量提取
- `xerintosh_draw_list_item` 拆分
- 选择器速度复位集中化
- 退场动画魔法数字常量化
- 弹窗换行 `sizeof` 保护
- 脏矩形升级到区域追踪

### App 层

- `app_menu.c` 拆分为 `app_menu_core.c` + `app_menu_entries.c`
- 显式化 `user_item` 生命周期契约
- 集中设置项 `level↔hw` 转换
- 补齐 `power_key_popup` native 测试

### 文档体系

- `doc/ui/index.md` 与 `doc/app/index.md` 使 `doc/` 镜像 `src/`
- `doc/tutorials/api-templates.md` 修复源码中断链引用
- `doc/index.md` 更新项目状态与重构记录

## 验证结果

- `pio test -e native`：**通过**（572 succeeded，2 skipped）
- `pio run -e m5stick-c`：**SUCCESS**，无新增警告
- `pio run -e m5stick-c-native`：**SUCCESS**，无新增警告
- 文档新增链接：全部有效
- 硬件冒烟测试：未执行，待后续配合实机验证

## 内存占用

| 环境 | RAM | Flash |
|---|---|---|
| `m5stick-c` | 19.5%（63832 / 327680 bytes） | 73.2%（1124673 / 1536000 bytes） |
| `m5stick-c-native` | 19.5%（63984 / 327680 bytes） | 73.9%（1135017 / 1536000 bytes） |

重构未引入明显内存增长，均保持在原有水平。

## 已知问题与后续工作

- 硬件冒烟测试（菜单导航、设置项切换、WiFi 菜单、电源键弹窗）需在实机上补测。
- `doc/` 中存在若干预存在断链，不属于本轮重构范围，建议后续集中修复。
- 脏矩形区域追踪已建立数据结构，渲染管线局部刷新实现留待后续迭代。

## 分支收尾

阶段 4 完成后，分支 `refactor/2026-06-27-fullstack` 进入收尾阶段。建议操作：

1. 合并回 `main`：本地合并并通过验证后删除分支。
2. 或发起 Pull Request：推送分支到远端并创建 PR，保留 worktree 以便迭代。
3. 或保持分支：由用户自行决定后续处理方式。

---

> **See Also:** [集成验证报告](03-integration.md) | [项目总览](README.md)
