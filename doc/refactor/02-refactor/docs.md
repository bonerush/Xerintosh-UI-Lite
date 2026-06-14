# Phase 2.5 文档同步报告

> **Parent:** [重构跟踪](../README.md) | **Prev:** [Phase 2.4 App 层重构](app.md)

## 1. 目标

在 App 层重构完成后，扫描并修复所有受影响的文档引用、示例代码和结构描述，确保 `doc/` 与 `src/`、`CLAUDE.md` 保持一致。

## 2. 扫描发现的问题

| ID | 文件 | 问题 | 修复方式 |
|---|---|---|---|
| D-1 | `CLAUDE.md` | App 层文件列表仍包含 `svc_mgr_helper.c/h`、`ui_service.c/h`，未体现拆分 | 更新为 `app_menu.c/h`、`app_input.c/h`、`app_state.c/h`、`ui_service.c/h` |
| D-2 | `doc/index.md` | App 层文件树与索引缺少 `app_menu`、`app_input`、`app_state`、`ui-service` | 更新文件树与索引表 |
| D-3 | `doc/app/index.md` | 缺少新模块入口 | 新增 app-menu/app-input/ui-service 入口 |
| D-4 | `doc/app/app-init.md` | 描述仍为大而全的 `app_init.c` | 重写为入口封装文档，指向 app-menu/app-input |
| D-5 | `doc/app/settings.md` | 仍包含已迁移的 `settings_flasher_role_label()` 章节 | 删除该章节 |
| D-6 | `doc/tutorials/api-templates.md` | 多处源链接指向 `app_init.c`，菜单示例/按键映射源链接错误 | 更新为 `app_menu.c`/`app_input.c` |
| D-7 | `doc/developer-guide.md` | 按键映射源链接、示例代码仍基于旧 `app_init.c` | 更新源链接，user_item 示例改用 `ui_service` |
| D-8 | `doc/tutorials/your-first-app.md` | 注册菜单的章节仍写 `app_init.c`/`app_init_ui()`，示例使用 `hal_input_reset_events()`/`ui_user_item_try_exit()` | 更新为 `app_menu.c`/`app_menu_build()` 和 `ui_service` |

## 3. 实施内容

### 3.1 文件新增

- `doc/app/app-menu.md`：菜单构建模块文档
- `doc/app/app-input.md`：输入处理模块文档
- `doc/app/ui-service.md`：UI 公共服务模块文档

### 3.2 文件重写/更新

- `doc/app/app-init.md`：从"大而全"重写为"入口封装"文档
- `doc/app/index.md`：增加新模块入口
- `doc/app/settings.md`：删除已迁移的 `settings_flasher_role_label` 章节
- `doc/index.md`：更新 App 层文件树与索引
- `CLAUDE.md`：更新 App 层文件列表与 flasher 目录说明

### 3.3 教程同步

- `doc/developer-guide.md`：
  - 源链接从 `app_init.c#L469-L494` 改为 `app_input.c#L33-L90`
  - user_item 示例改用 `ui_service_user_item_init/loop/exit`
  - 完整示例源链接改为 `app_menu.c#L32-L103`
- `doc/tutorials/api-templates.md`：
  - 模板 2 菜单构建示例源链接改为 `app_menu.c`
  - 模板 3 `init_function` 示例改为 `flasher_menu.c`
  - 模板 5 默认按键映射源链接改为 `app_input.c`
- `doc/tutorials/your-first-app.md`：
  - 架构图 App 层文件改为 `app_menu.c`
  - 示例代码改用 `ui_service` 入口
  - 注册菜单章节改为 `app_menu.c`/`app_menu_build()`
  - 伪代码同步改为 `ui_service_user_item_exit()`

## 4. 验证

- 无新增 native 测试（本次为纯文档同步）。
- `pio test -e native` 通过。
- `pio run -e m5stick-c` 编译通过。
- 人工 grep 确认 `doc/` 中不再引用旧 `app_init.c` 行号段、`settings_flasher_role_label`、`svc_mgr_helper` 等过时内容。

## 5. 已知未处理

- 部分旧文档仍直接调用 `ui_user_item_try_exit()` 而非 `ui_service_user_item_loop()`，这是允许的：
  - `ui_service_user_item_loop()` 是对 `ui_user_item_try_exit()` 的薄封装
  - 在 user_item 内部直接调用 `ui_user_item_try_exit()` 仍然有效
  - 教程和开发者指南已统一推荐使用 `ui_service` 接口

