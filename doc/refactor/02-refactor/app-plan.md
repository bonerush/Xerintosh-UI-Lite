# Phase 2.2 App 上层重构计划

**工作目录**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-first`  
**日期**：2026-06-14  
**基线**：内核层改动已完成，`pio test -e native` 通过

---

## 1. 本轮 App 层目标

本轮优先处理 **与内核改动直接相关或安全风险较高、且改动面可控** 的 App 上层问题，确保每个子任务可独立验证、先写测试后改代码。

**本轮处理的问题 ID**：

| ID | 问题 | 优先级 |
|---|---|---|
| A-P1-01 | `app_menu_build()` 未检查 `xerintosh_new_*_item()` 返回值 | P1 |
| A-P1-05 | 串口监视器 / 烧录器进入退出横屏代码重复 | P1 |
| A-P1-06 | 串口监视器直接管理 BT 生命周期，未走统一异步请求助手 | P1 |
| A-P2-01 | `settings_set_rotation()` 未校验输入范围 | P2 |
| A-P2-02 | `tu_app` API key 为空时也调用网络请求 | P2 |

**延后处理的问题 ID**：

| ID | 延后原因 |
|---|---|
| A-P1-02 | `wifi_manager.cpp` 过长、状态机职责过重，需跨模块设计公共状态机模板 |
| A-P1-03 | WiFi/BT enable/disable/request 异步状态机模式重复，依赖 A-P1-02 的模板 |
| A-P1-04 | taskmgr 硬编码任务名禁用服务，需要先设计任务退出回调或通用服务禁用机制 |

---

## 2. 子任务列表

### APP-01：菜单构建 NULL 检查（A-P1-01）

- **变更文件**：
  - `src/app/app_menu.c`
  - `src/app/app_menu.h`
  - `test/test_native/test_app_menu_safety.cpp`

- **步骤**：
  1. 在 `app_menu.c` 中引入 `kernel/debug_serial.h`。
  2. 新增内部辅助函数：

     ```c
     bool app_menu_push_checked(xerintosh_list_item_t *parent,
                                xerintosh_list_item_t *child,
                                const char *name);
     ```

     功能：若 `child == NULL` 或 `xerintosh_push_item_to_list()` 失败，打印错误日志并返回 `false`。
  3. 在 `app_menu.h` 中声明该辅助函数（标注为内部使用）。
  4. 重写 `app_menu_build()`：
     - 每个 `xerintosh_new_*_item()` 返回后立即判断是否 `NULL`，为 `NULL` 时记录日志并跳过该节点；
     - 所有 `xerintosh_push_item_to_list()` 替换为 `app_menu_push_checked()`；
     - 波特率循环中每个 `btn` 创建失败时 `continue`。
  5. 行为保持与现有菜单结构一致。

- **测试**：
  - 先写 `test_app_menu_safety.cpp`：
    - `AppMenuPushChecked_NullChild_ReturnsFalse`：直接传入 `NULL` 子项，验证返回 `false`。
    - `AppMenuBuild_NormalPath_HasExpectedChildren`：正常调用 `app_menu_build()`，验证根菜单子项数 ≥ 6，设置菜单子项数 ≥ 7。
    - 运行 `pio test -e native`。

- **回滚策略**：还原 `app_menu.c/h`，删除测试文件。

- **验收标准**：
  - `pio test -e native` 通过。
  - `pio run -e m5stick-c` 无新 warning。
  - 正常启动后菜单结构与重构前完全一致。

---

### APP-05：提取公共横屏辅助函数（A-P1-05）

- **变更文件**：
  - `src/app/ui_service.c`
  - `src/app/ui_service.h`
  - `src/app/serial_monitor/sm_app.cpp`
  - `src/app/flasher/flasher_app.cpp`
  - `test/test_native/test_ui_service_landscape.cpp`
  - `doc/app/ui-service.md`

- **步骤**：
  1. 在 `ui_service.h` 中新增接口：

     ```c
     void ui_service_enter_landscape(void);
     void ui_service_exit_landscape(void);
     ```

  2. 在 `ui_service.c` 中实现：
     - 使用文件级静态变量保存进入前的 `g_is_landscape` 状态；
     - 进入时：若当前不是横屏，则设置 `g_is_landscape = true`、`g_screen_rotation_level = ORIENTATION_LANDSCAPE`，调用 `hal_display_set_rotation(1)`、`hal_screen_get_size()`、`hal_display_init()`；
     - 退出时：若之前不是横屏，则恢复竖屏并调用对应 HAL 函数。
  3. 在 `sm_app.cpp` 的 `serial_monitor_init()` / `serial_monitor_exit()` 中，用 `ui_service_enter_landscape()` / `ui_service_exit_landscape()` 替换原横屏代码块。
  4. 在 `flasher_app.cpp` 的 `flasher_init()` / `flasher_exit()` 中做同样替换。
  5. 移除 `sm_app.cpp` 与 `flasher_app.cpp` 中各自的 `s_prev_landscape` 静态变量。
  6. 更新 `doc/app/ui-service.md` 说明新的横屏辅助函数。

- **测试**：
  - 先写 `test_ui_service_landscape.cpp`：
    - `EnterLandscape_FromPortrait_SwitchesToLandscape`：初始竖屏 → 进入后 `g_is_landscape == true`、`hal_display_get_rotation() == 1`。
    - `ExitLandscape_RestoresPortrait`：进入后再退出 → 恢复竖屏、`hal_display_get_rotation() == 0`。
    - `EnterLandscape_AlreadyLandscape_NoChange`：初始横屏 → 进入后状态不变。
  - 运行 `pio test -e native`。

- **回滚策略**：还原 `ui_service.c/h`、两个 App 文件、删除测试文件。

- **验收标准**：
  - `pio test -e native` 通过。
  - `pio run -e m5stick-c` 通过。
  - 串口监视器和烧录器进入/退出时的屏幕旋转行为与重构前一致。

---

### APP-06：串口监视器使用服务管理助手请求 BT（A-P1-06）

> 注：原 `svc_mgr_helper.c/h` 可能已被移除，本轮为“App 请求服务生命周期”这一模式重建一个最小化的 `svc_mgr_helper`，避免各 App 直接调用同步 `bt_mgr_disable()`。

- **变更文件**：
  - `src/app/svc_mgr_helper.c`（新建或修改）
  - `src/app/svc_mgr_helper.h`（新建或修改）
  - `src/app/serial_monitor/sm_app.cpp`
  - `src/app/bluetooth/bt_manager.h`
  - `src/app/bluetooth/bt_manager.cpp`
  - `test/test_native/test_svc_mgr_helper.cpp`
  - `doc/app/svc-mgr-helper.md`

- **步骤**：
  1. 在 `bt_manager.h` 的 `#ifdef NATIVE_TEST` 块中新增测试接缝：

     ```c
     void bt_mgr_test_set_enabled(bool enabled);
     ```

     在 `bt_manager.cpp` 的 native 桩中用全局变量 `g_bt_enabled_test` 实现 `bt_mgr_is_enabled()` 返回该变量。
  2. 新建/修改 `svc_mgr_helper.h`：

     ```c
     void svc_mgr_bt_request_enable(bool *lazy_inited);
     void svc_mgr_bt_request_disable(bool *lazy_inited);
     ```

  3. 新建/修改 `svc_mgr_helper.c`，实现：
     - `svc_mgr_bt_request_enable`：若 `bt_mgr_is_enabled()` 为 false，则调用 `bt_mgr_request_enable()` 并将 `*lazy_inited` 置 true。
     - `svc_mgr_bt_request_disable`：若 `*lazy_inited` 为 true 且 `bt_mgr_is_enabled()` 为 true，则调用 `bt_mgr_request_disable()` 并将 `*lazy_inited` 置 false。
     - 对 `lazy_inited == NULL` 做防御性返回。
  4. 在 `sm_app.cpp` 中：
     - 包含 `svc_mgr_helper.h`；
     - 将 `bt_mgr_request_enable(); sm_bt_lazy_inited = true;` 替换为 `svc_mgr_bt_request_enable(&sm_bt_lazy_inited);`；
     - 将退出时的 `if (sm_bt_lazy_inited && bt_mgr_is_enabled()) { bt_mgr_disable(); ... }` 替换为 `svc_mgr_bt_request_disable(&sm_bt_lazy_inited);`。
  5. 更新 `doc/app/svc-mgr-helper.md`，说明该模块已恢复为 BT 懒加载助手。

- **测试**：
  - 先写 `test_svc_mgr_helper.cpp`：
    - `RequestEnable_WhenDisabled_SetsLazyFlag`：`bt_mgr_test_set_enabled(false)` → 调用 enable → `lazy == true`。
    - `RequestEnable_WhenEnabled_DoesNotSetLazyFlag`：`bt_mgr_test_set_enabled(true)` → 调用 enable → `lazy == false`。
    - `RequestDisable_WhenLazyAndEnabled_ClearsFlag`：先 enable，再 disable → `lazy == false` 且 disable 请求发出。
    - `RequestDisable_WhenNotLazy_DoesNothing`：`lazy == false` → 调用 disable 后仍为 false。
  - 运行 `pio test -e native`。

- **回滚策略**：删除/还原 `svc_mgr_helper.c/h`，还原 `sm_app.cpp` 与 `bt_manager.h/cpp` 的测试接缝，删除测试文件。

- **验收标准**：
  - `pio test -e native` 通过。
  - `pio run -e m5stick-c` 通过。
  - 串口监视器 BLE 模式切换与退出逻辑行为不变，且不再在 UI 任务上下文中同步调用 `bt_mgr_disable()`。

---

### APP-P2-01：`settings_set_rotation()` 输入校验（A-P2-01）

- **变更文件**：
  - `src/app/settings/settings.c`
  - `test/test_native/test_settings_accessors.cpp`

- **步骤**：
  1. 修改 `settings_set_rotation(int16_t level)`：
     - 若 `level` 不是 `ORIENTATION_PORTRAIT` 或 `ORIENTATION_LANDSCAPE`，则将其钳位为 `ORIENTATION_LANDSCAPE`；
     - 然后再赋值并同步 `g_is_landscape`。

- **测试**：
  - 先在 `test_settings_accessors.cpp` 中新增：
    - `RotationClampsInvalidLow`：`settings_set_rotation(0)` → `settings_get_rotation() == ORIENTATION_LANDSCAPE`。
    - `RotationClampsInvalidHigh`：`settings_set_rotation(99)` → `settings_get_rotation() == ORIENTATION_LANDSCAPE`。
  - 运行 `pio test -e native`。

- **回滚策略**：还原 `settings.c` 与测试文件。

- **验收标准**：
  - 新增测试通过。
  - 原有 `RotationSetPortrait` 等测试仍通过。
  - 硬件编译无 warning。

---

### APP-P2-02：`tu_app` API key 为空时跳过请求（A-P2-02）

- **变更文件**：
  - `src/app/token_usage/tu_app.cpp`
  - `src/app/token_usage/tu_app.h`
  - `test/test_native/test_tu_app.cpp`

- **步骤**：
  1. 在 `tu_app.cpp` 的 `token_usage_loop()` 中，获取 `ds_key` 后增加判断：

     ```c
     bool has_key = storage_get_deepseek_key(ds_key, sizeof(ds_key));
     if (has_key && ds_key[0] != '\0') {
         g_tu_data.deepseek_ok = tu_api_fetch_deepseek(ds_key, &g_tu_data.deepseek);
     } else {
         g_tu_data.deepseek_ok = false;
     }
     ```

  2. 保持 `g_tu_data.last_update` 与 `g_last_refresh` 的更新逻辑，确保不会每帧重复触发。
  3. 在 `tu_app.h` 中新增测试可见的只读 getter：

     ```c
     const tu_data_t* token_usage_get_data(void);
     ```

     并在 `tu_app.cpp` 中实现返回 `&g_tu_data`。

- **测试**：
  - 先写 `test_tu_app.cpp`：
    - `Loop_WithEmptyKey_SkipsFetch`：调用 `token_usage_init(NULL)` 和 `token_usage_loop(NULL)`，断言 `token_usage_get_data()->deepseek_ok == false`（修复前为空 key 会返回 true）。
  - 运行 `pio test -e native`。

- **回滚策略**：还原 `tu_app.cpp/h`，删除测试文件。

- **验收标准**：
  - `pio test -e native` 通过。
  - `pio run -e m5stick-c` 通过。
  - 未配置 API key 时不再发起网络请求。

---

## 3. 依赖关系图

```text
APP-01 (app_menu NULL 检查) ──┐
                              ├──→ 可并行
APP-P2-01 (rotation 校验) ────┤
                              │
APP-P2-02 (tu_app 空 key) ────┤
                              │
APP-05 (横屏helper) ──────────┤
                              │
                              └──→ APP-06 (BT service helper)
                                   （两者都修改 sm_app.cpp，需串行或合并提交）
```

**执行顺序建议**：

1. APP-01、APP-P2-01、APP-P2-02 可并行独立开发验证。
2. APP-05 先行完成并验证。
3. APP-06 在 APP-05 之后进行，因为两者都修改 `sm_app.cpp`，避免冲突。

---

## 4. 风险与回退点

| 风险 | 影响 | 回退点 |
|---|---|---|
| `app_menu_build` 增加 NULL 检查后，某处未正确跳过导致菜单缺项 | 功能 | 还原 `app_menu.c/h` |
| `ui_service` 横屏helper 静态状态跨 App 未清理 | 显示方向异常 | 还原 `ui_service.c/h` 与两个 App 的调用 |
| `svc_mgr_helper` 异步请求语义与 `sm_app` 原同步 disable 不完全等价 | BLE 退出后未释放内存 | 还原 `svc_mgr_helper.c/h` 与 `sm_app.cpp` |
| `settings_set_rotation` 钳位策略与调用方预期不一致 | 设置保存行为 | 还原 `settings.c` |
| `tu_app` 跳过请求后 UI 状态未正确刷新 | 显示旧数据 | 还原 `tu_app.cpp/h` |

每个子任务均有独立回滚策略，可随时单点还原而不影响其他子任务。

---

## 5. 测试策略

- **单元测试**：每个子任务先在 `test/test_native/` 下新增对应测试文件，覆盖正常路径与异常路径。
- **回归测试**：每个子任务修改后执行：
  - `pio test -e native`
  - `pio run -e m5stick-c`
- **集成验证**（硬件条件允许时）：
  - 菜单结构、进入/退出串口监视器、进入/退出烧录器、BLE 源切换、Token Usage 空 key 显示均与重构前一致。
- **TDD 顺序**：测试文件先行提交，代码修改随后提交，确保每次失败/成功都有明确对应。

---

### Critical Files for Implementation

- `src/app/app_menu.c`
- `src/app/ui_service.c` / `src/app/ui_service.h`
- `src/app/serial_monitor/sm_app.cpp`
- `src/app/svc_mgr_helper.c` / `src/app/svc_mgr_helper.h`
- `src/app/settings/settings.c`
- `src/app/token_usage/tu_app.cpp`
