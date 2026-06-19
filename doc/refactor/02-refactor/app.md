# App 层重构报告（第十轮 · 2026-06-19）

## 范围
- 处理诊断问题：D9, D10, D11, D15
- 变更文件：
  - `src/app/app_menu.c` — 添加蓝牙开关
  - `src/app/taskmgr/taskmgr_app.c` — 异步 BT 关闭
  - `src/app/wifi/wifi_manager.cpp` — popup 跨任务保护
  - `src/app/bluetooth/bt_uart_service.cpp` — 注释修正

## 变更摘要
| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改文件 | 4 | |
| 新增行 | ~20 | switch_item 创建、spinlock 保护 |
| 修改行 | ~10 | bt_mgr_request_disable、注释修正 |
| 删除行 | ~5 | 旧 TODO 注释 |

## 详细变更

### 1. 设置菜单添加蓝牙开关 (D9)
**原因**：`build_settings_items()` 只创建了 WiFi 开关，蓝牙开关完全缺失，用户无法从 UI 启停蓝牙。

**实现**：在 WiFi 开关后添加 `xerintosh_new_switch_item("蓝牙", &g_bt_on, NULL, bt_mgr_on_switch_toggle, default_icon)`，并调用 `app_menu_push_checked()` 挂载。

**文件**：`src/app/app_menu.c:76-77, 104`

### 2. taskmgr 使用异步 BT 关闭 (D10)
**原因**：`taskmgr_app.c:214` 在 UI 任务上下文中直接调用同步 `bt_mgr_disable()`，违反"同步接口仅主任务可调用"的约束，可能导致 Bluedroid 死锁→TWDT 复位。

**实现**：替换为 `bt_mgr_request_disable()`（异步接口）。

**文件**：`src/app/taskmgr/taskmgr_app.c:214`

### 3. g_popup_content 跨任务保护 (D11)
**原因**：WiFi 任务写入 `g_popup_content`，UI 任务读取，`strncpy` 非原子操作可能产生乱码。

**实现**：添加 `portMUX_TYPE g_popup_spinlock`，在 `wifi_popup_request()`（写入端）和 `wifi_popup_refresh()`（读取端）使用 `portENTER_CRITICAL`/`portEXIT_CRITICAL` 保护临界区。

**文件**：`src/app/wifi/wifi_manager.cpp:93-142`

### 4. g_wifi_on 默认值注释修正 (D15)
**原因**：`wifi_manager.cpp` 和 `bt_uart_service.cpp` 注释称"WiFi 默认关闭"，但实际 `app_state.c` 中 `g_wifi_on = true`。

**实现**：修正注释为"WiFi 默认开启 (g_wifi_on=true)"。

**文件**：`wifi_manager.cpp:184`, `bt_uart_service.cpp:188`

## 测试
- 验证结果：
  - `pio run -e m5stick-c`：✅ PASS (RAM 27.0%, Flash 89.2%)
  - `pio test -e native`：✅ 224/225 通过

## 检查清单
- [x] 蓝牙开关正确绑定 `g_bt_on` 和 `bt_mgr_on_switch_toggle`
- [x] taskmgr 使用异步 BT 接口
- [x] spinlock 正确初始化和配对使用
- [x] 注释与代码一致
- [x] 硬件构建无新增警告
