# App 上层重构报告

## 范围

- 处理诊断问题：A-P1-01、A-P1-05、A-P1-06、A-P2-01、A-P2-02
- 延后问题：A-P1-02、A-P1-03、A-P1-04

## 变更摘要

| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改文件 | 9 | App 源码与测试 |
| 新增文件 | 4 | `svc_mgr_helper.c/h`、`test_app_menu_safety.cpp`、`test_svc_mgr_helper.cpp` |
| 新增 API | 4 | `ui_service_enter_landscape`、`ui_service_exit_landscape`、`svc_mgr_bt_request_enable`、`svc_mgr_bt_request_disable` |
| 行为修复 | 3 | 菜单 NULL 检查、rotation 输入校验、空 API key 跳过网络请求 |
| 新增测试 | 14+ | 覆盖菜单安全、横屏 helper、BT 服务助手、settings、token usage |

## 详细变更

### 1. APP-01：菜单构建 NULL 检查（A-P1-01）
**原因**：A-P1-01  
**实现**：
- 在 `src/app/app_menu.c` 中新增 `static app_menu_push_checked()` 辅助函数。
- `app_menu_build()` 中每个 `xerintosh_new_*_item()` 返回后检查 NULL，失败时记录日志并跳过。
- 所有 `xerintosh_push_item_to_list()` 替换为 `app_menu_push_checked()`。
**影响接口**：无 public API 变化（辅助函数为 static）。  
**文档更新**：`doc/app/app-init.md`（阶段 2.5 更新）。

### 2. APP-05：提取公共横屏辅助函数（A-P1-05）
**原因**：A-P1-05  
**实现**：
- 在 `src/app/ui_service.h` 新增 `ui_service_enter_landscape()` / `ui_service_exit_landscape()`。
- 在 `src/app/ui_service.c` 中用静态变量保存进入前方向状态，进入/退出时统一设置 HAL 旋转。
- `src/app/serial_monitor/sm_app.cpp` 和 `src/app/flasher/flasher_app.cpp` 移除本地状态变量，改为调用 helper。
**影响接口**：新增 2 个 public helper。  
**文档更新**：`doc/app/ui-service.md`（已部分更新，阶段 2.5 补充）。

### 3. APP-06：串口监视器使用服务管理助手请求 BT（A-P1-06）
**原因**：A-P1-06  
**实现**：
- 新建 `src/app/svc_mgr_helper.c` / `src/app/svc_mgr_helper.h`。
- 新增 `svc_mgr_bt_request_enable(bool *lazy_inited)` / `svc_mgr_bt_request_disable(bool *lazy_inited)`。
- `src/app/bluetooth/bt_manager.h` / `bt_manager.cpp` 新增 `bt_mgr_test_set_enabled()` 测试接缝。
- `src/app/serial_monitor/sm_app.cpp` 改用 helper，不再直接调用 `bt_mgr_request_enable()` / `bt_mgr_disable()`。
**影响接口**：新增 2 个 public helper 和 1 个测试接缝。  
**文档更新**：`doc/app/svc-mgr-helper.md`（阶段 2.5 更新）。

### 4. APP-P2-01：`settings_set_rotation()` 输入校验（A-P2-01）
**原因**：A-P2-01  
**实现**：
- `src/app/settings/settings.c` 中 `settings_set_rotation()` 增加校验：非法值钳位到 `ORIENTATION_LANDSCAPE`。
**影响接口**：无 public API 变化。  
**文档更新**：`doc/app/settings.md`（阶段 2.5 更新）。

### 5. APP-P2-02：`tu_app` API key 为空时跳过请求（A-P2-02）
**原因**：A-P2-02  
**实现**：
- `src/app/token_usage/tu_app.cpp` 中 `token_usage_loop()` 增加空 key 判断，空 key 时跳过 `tu_api_fetch_deepseek()`。
- `src/app/token_usage/tu_app.h` 新增 `token_usage_get_data()` 测试 getter。
**影响接口**：新增 1 个测试可见 getter。  
**文档更新**：`doc/app/token-usage.md`（阶段 2.5 更新）。

## 测试

- 新增测试：
  - `test/test_native/test_app_menu_safety.cpp`
  - `test/test_native/test_ui_service_landscape.cpp`
  - `test/test_native/test_svc_mgr_helper.cpp`
  - `test/test_native/test_settings_accessors.cpp` 新增 rotation 校验用例
  - `test/test_native/test_tu_app.cpp`
- 验证结果：
  - `pio test -e native`：✅ PASS（409 个测试用例，1 个 skipped，408 个 succeeded）
  - `pio run -e m5stick-c`：✅ PASS（Flash 88.0%，RAM 22.3%）

## 检查清单

- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位（无新增继承）
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`（无新增回调）
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [ ] 文档已同步更新（阶段 2.5 统一处理）
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点

- 每个子任务均为独立 commit，可单独 `git revert <commit>`。
- 统一回滚到阶段 2.2 开始前：`git reset --hard 3d47845`（T1 内核提交）。

## 遗留问题

| ID | 问题 | 后续处理 |
|----|------|----------|
| A-P1-02 | `wifi_manager.cpp` 文件过长、状态机职责过重 | 延后，需设计公共状态机模板 |
| A-P1-03 | WiFi/BT enable/disable/request 异步状态机重复 | 依赖 A-P1-02 |
| A-P1-04 | taskmgr 硬编码任务名禁用服务 | 延后，需任务退出回调或通用服务禁用机制 |
