# 文档体系重构报告

> **Parent:** [doc/refactor/README.md](../README.md) | **Prev:** [App 层重构报告](app.md)

## 目标

让 `doc/` 目录结构更接近 `src/` 源码结构，修复源码注释中的断链引用，并在中央索引中记录本轮全栈重构的进展，使新开发者能够快速从源码定位到对应文档。

## 变更摘要

### 1. 镜像 `src/` 结构（D1）

新增 `doc/ui/` 与 `doc/app/` 子目录，分别对应 `src/ui/` 与 `src/app/`：

- `doc/ui/index.md`：UI 核心框架索引，链接到 `ui_types.h`、`ui_item_core.h`、`ui_dispatch.c`、选择器、列表绘制、脏区域等关键源文件。
- `doc/app/index.md`：App 层索引，链接到 `app_menu_core.c`、`app_menu_entries.c`、`user_item_contract.h`、`settings.c`、`power_key_popup.c` 等关键源文件。

*📄 Source: [doc/ui/index.md](../../../doc/ui/index.md)*
*📄 Source: [doc/app/index.md](../../../doc/app/index.md)*

### 2. 更新中央索引（D2）

更新 `doc/index.md`：

- 在架构概览中加入 UI 框架与应用层的文档链接。
- 在文档树中新增 UI 核心框架、App 层、API 模板教程的入口。
- 将“当前项目状态”中内核层“进行中”更新为已完成，并新增 UI、App、文档重构完成的状态。
- 新增“重构记录”表格，记录 2026-06-27 全栈重构的范围与报告入口。

*📄 Source: [doc/index.md](../../../doc/index.md)*

### 3. 修复断链引用（D3）

`src/ui/ui_item_core.h` 注释引用 `doc/tutorials/api-templates.md`，但该文件不存在。新建 `doc/tutorials/api-templates.md`，提供常用 API 调用模板与常见陷阱说明，使源码引用重新生效。

*📄 Source: [doc/tutorials/api-templates.md](../../../doc/tutorials/api-templates.md)*
*📄 Source: [ui_item_core.h](../../../src/ui/ui_item_core.h#L8)*

## 新增 / 修改文档

| 文档 | 类型 | 说明 |
|---|---|---|
| `doc/ui/index.md` | 新增 | UI 核心框架知识索引 |
| `doc/app/index.md` | 新增 | App 层知识索引 |
| `doc/tutorials/api-templates.md` | 新增 | API 模板与常见陷阱 |
| `doc/index.md` | 修改 | 更新架构概览、文档树、项目状态、重构记录 |

## 验证结果

- `pio test -e native`：**通过**（574 cases，2 skipped，572 succeeded）
- `pio run -e m5stick-c`：**通过**，无新增编译警告
- `pio run -e m5stick-c-native`：**通过**，无新增编译警告
- 内部链接检查：`doc/ui/index.md`、`doc/app/index.md`、`doc/tutorials/api-templates.md` 路径存在，`doc/index.md` 中指向这些文件的链接有效。

## 风险与后续注意

- `doc/ui/index.md` 与 `doc/app/index.md` 目前为索引级文档，后续应按模块拆分更详细的子文档（如 `ui/selector.md`、`app/settings.md`）。
- `doc/tutorials/api-templates.md` 应与源码保持同步；新增 UI 项类型或 App 注册方式变化时，需同步更新该教程。
- HAL 层目前缺少对应文档，后续可补充 `doc/hal/index.md`。

## 相关提交

```
eafd1a1 docs(refactor): add App layer refactor report and mark app as DONE
<当前提交>  docs(refactor): add docs layer refactor report and mark docs as DONE
```

---

> **See Also:** [App 层重构报告](app.md) | [下一阶段：集成验证](../03-integration.md)
