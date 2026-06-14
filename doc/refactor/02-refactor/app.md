# Phase 2.4 App 层重构报告

> **Parent:** [重构跟踪](../README.md) | **Prev:** [Phase 2.3 UI 核心层重构](ui.md)

## 1. 目标

解决 App 层当前最突出的结构性问题：

1. `app_init.c` 职责过重（菜单构建 + 输入路由 + 烧录器业务逻辑）。
2. flasher 引脚配置/强制解除状态机侵入 `app_init.c`。
3. WiFi/BT/设置相关全局状态散乱，多处 `extern` 声明。
4. `ui_service.c/h` 缺失，各 `user_item` App 生命周期实现不统一。
5. settings 模块反向依赖 flasher。

## 2. 诊断摘要

| ID | 优先级 | 问题 | 位置 |
|---|---|---|---|
| A-P0-1 | P0 | `app_init.c` 同时承担菜单构建、输入路由、管理器初始化、烧录器业务逻辑 | `src/app/app_init.c:147-495` |
| A-P0-2 | P0 | `app_input_process()` 混合电源键弹窗、WiFi 弹窗、延迟弹窗、烧录器状态机、框架导航 | `src/app/app_init.c:372-495` |
| A-P0-3 | P0 | flasher 引脚配置/强制解除状态机实现位于 `app_init.c` | `src/app/app_init.c:58-467` |
| A-P1-1 | P1 | WiFi/BT 弹窗与 `app_input_process()` 过度耦合 | `src/app/app_init.c:381-384,479-483` |
| A-P1-2 | P1 | 全局状态（`g_wifi_on`、`g_bt_on`、设置回调）散乱 | `main.cpp`、`app_init.c`、`settings.c` |
| A-P1-3 | P1 | `user_item` App 生命周期与动画不统一 | 各 `src/app/*/` |
| A-P1-4 | P1 | `settings.c` 反向依赖 `flasher_gpio.h` | `src/app/settings/settings.c:13,180-189` |
| A-P1-5 | P1 | `ui_service.c/h`、`svc_mgr_helper.c/h` 实际缺失但文档仍列出 | `CLAUDE.md`、`doc/index.md` |
| A-P2-3 | P2 | `ui_task.c` 直接调用 Arduino `delay()` | `src/app/ui_task.c:75` |
| A-P2-4 | P2 | `on_baud_selected_cb` 未使用回调参数 `ud` | `src/app/app_init.c:240-248` |

## 3. 实施计划

### Step 1 — 拆分 `app_init.c`

- 新建 `src/app/app_menu.c/h`：负责菜单树构建（`app_menu_build()`）。
- 新建 `src/app/app_input.c/h`：负责输入路由（`app_input_process()`）。
- 保留 `src/app/app_init.c/h`：仅作为入口封装，调用 `app_menu_build()` 与 `app_input_process()`。

### Step 2 — 迁回 flasher 逻辑

- 新建 `src/app/flasher/flasher_menu.c/h`。
- 将 `g_flasher_pin_menu`、`g_flasher_sub_state`、`safe_set_content()`、`update_flasher_pin_label()`、`on_enter_flasher_submenu()`、`on_g36_pressed_cb()`、`on_flasher_role_selected_cb()` 及强制解除状态机迁移到 `flasher_menu.c/h`。
- `app_menu.c` 仅调用 `flasher_menu_get_root()` 获取烧录器引脚子菜单并挂载。

### Step 3 — 收敛全局状态

- 新建 `src/app/app_state.c/h`。
- 将 `g_wifi_on`、`g_bt_on` 从 `main.cpp` 移至 `app_state.c`。
- 将设置变更回调声明集中到 `app_state.h`。
- `main.cpp`、`app_init.c`、WiFi/BT 管理器统一包含 `app_state.h`。

### Step 4 — 重建 `ui_service`

- 新建 `src/app/ui_service.c/h`。
- 提供 `ui_user_item_template_init/loop/exit` 或 `ui_user_item_begin()` / `ui_user_item_end()` 工具函数。
- 统一处理：按键事件重置、屏幕方向保存/恢复、标准退出检测。
- 本次先提供 API，后续各 App 逐步迁移；本次至少让 `about.c` 使用统一入口。

### Step 5 — 解耦 settings 与 flasher

- 将 `settings_flasher_role_label()` 从 `settings.c` 移到 `flasher_gpio.c/h`（或新建 `flasher_text.c/h`）。
- `settings.c` 不再包含 `flasher_gpio.h`。

### Step 6 — 修复 P2 细节

- `ui_task.c`：`delay(1)` → `hal_delay_ms(1)`。
- 波特率按钮 `user_data` 直接使用 `ud`，避免回调中读取全局选择器。

### Step 7 — 文档同步

- 更新 `doc/refactor/README.md`，标记 phase 2.4 为 `DONE`。
- 更新 `CLAUDE.md`、`doc/index.md` 中 `ui_service`、`svc_mgr_helper` 描述。
- 更新 `doc/app/app-init.md` 等文档以反映新文件结构。

## 4. 风险与约束

- 不修改 UI 核心层、HAL 层、内核层（已在本轮前序阶段完成）。
- 每步修改前先补充 native 测试，确保 `pio test -e native` 通过。
- `app_init.h` 的公共 API（`app_init_ui()`、`app_init_managers()`、`app_input_process()`）保持不变，避免 `main.cpp` 改动过大。
- flasher 的菜单状态机迁回后，行为必须与迁回前一致。

## 5. 验证标准

- `pio test -e native` 通过。
- `pio run -e m5stick-c` 编译通过且无新 warning。
- 菜单结构、烧录器引脚配置流程、WiFi/BT 开关行为与重构前一致。

## 6. 实际完成摘要

### 代码改动

- 新增 `src/app/app_state.c/h`：集中定义 `g_wifi_on`、`g_bt_on` 与设置变更回调声明。
- 新增 `src/app/app_menu.c/h`：从 `app_init.c` 拆分出菜单构建逻辑。
- 新增 `src/app/app_input.c/h`：从 `app_init.c` 拆分出输入处理逻辑。
- 新增 `src/app/ui_service.c/h`：提供 `ui_service_user_item_init/loop/exit` 公共模板。
- 新增 `src/app/flasher/flasher_menu.c/h`：将烧录器引脚配置 UI、角色选择回调、强制解除状态机从 `app_init.c` 迁回 flasher 模块。
- 精简 `src/app/app_init.c`：仅保留入口封装，调用 `app_menu_build()` 与 `app_input_process()`。
- 将 `settings_flasher_role_label()` 迁移为 `flasher_role_label()`，移除 `settings.c` 对 `flasher_gpio.h` 的反向依赖。
- 修复 `src/app/ui_task.c` 直接调用 Arduino `delay()` 的问题，改为 `hal_delay_ms(1)`。
- 波特率选择回调改为直接使用 `user_data`，不再读取全局选择器。
- 迁移 `about.c` 使用 `ui_service` 提供的统一入口。

### 测试

- 新增 `test/test_native/test_app_state.cpp`（2 个用例）。
- 新增 `test/test_native/test_flasher_menu.cpp`（3 个用例）。
- `pio test -e native` 通过（371 tests passed）。
- `pio run -e m5stick-c` 成功，Flash 占用 88.0%，RAM 占用 22.3%。

### 文档

- 更新 `doc/app/app-init.md` 反映新文件结构与入口封装。
- 新增 `doc/app/app-menu.md`、`doc/app/app-input.md`、`doc/app/ui-service.md`。
- 更新 `doc/app/settings.md` 移除已迁移的 `settings_flasher_role_label` 章节。
- 更新 `CLAUDE.md`、`doc/index.md` 中 App 层文件列表与文档树。
- 更新 `doc/refactor/README.md` 标记 phase 2.4 为 `DONE`。
