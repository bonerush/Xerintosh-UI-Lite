# 诊断报告：App 层（第十一轮 · 2026-06-19）

## 方法
- 静态扫描范围：`src/app/` 及关联测试文件
- 上一轮已修复：设置菜单蓝牙开关、taskmgr 异步禁用蓝牙、WiFi popup spinlock 保护、蓝牙注释修正
- 本轮重点关注：WiFi/蓝牙状态机、串口输入/监视器、taskmgr、settings、user_item 生命周期模板

## 优先级定义
- **P0**：会导致崩溃、功能完全不可用、数据损坏
- **P1**：竞态条件、状态机缺陷、可维护性问题，可能在特定场景下演变为 P0
- **P2**：风格、文档、重复代码、测试缺失
- **P3**：细节不一致、死代码、轻微优化点

---

## P0 问题

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| APP-P0-01 | taskmgr | `src/app/taskmgr/taskmgr_app.c` | 212 | **UI 任务仍同步调用 `wifi_mgr_disable()`**。与第十轮已修复的 `bt_mgr_disable()` 问题同类：taskmgr 作为 user_item 运行在 Xeros UI 任务，直接关闭 WiFi 驱动，而 WiFi 操作应在与 `wifi_mgr_enable()` 相同的上下文执行，跨任务调用可能导致驱动状态不一致、内存泄漏或异常复位。 | 仿照 bt-mgr 增加 `wifi_mgr_request_disable/enable()` 异步请求接口，并在 `main.cpp` 的 loop 中新增 `wifi_mgr_process_requests()` 统一执行；taskmgr 只发请求。 | 缺少；需新增 `test_taskmgr_wifi_async.cpp` |
| APP-P0-02 | 蓝牙 UART 服务 | `src/app/bluetooth/bt_uart_service.cpp` | 270-305 | **`bt_uart_service_deinit()` TOCTOU 竞态未修复**。仍用 `delay(100)` 盲等 `bt_uart_poll()` 退出。若 poll 正在执行 `g_bt_serial.connected()/read()` 或向 `g_rx_queue` 写入时调用 `end()` / `vQueueDelete()`，可能导致 Bluedroid 崩溃或队列删除时数据竞争。 | 使用 FreeRTOS 任务通知/二值信号量：`deinit` 设置 shutdown 标志后等待 poll 完成信号（带超时），再释放资源；`poll` 在退出临界路径前给出信号。 | `test_ble_uart` 未覆盖；新增 race/deinit 测试 |
| APP-P0-03 | WiFi/BT 互斥 | `src/app/wifi/wifi_manager.cpp:195-228`<br>`src/app/bluetooth/bt_manager.cpp:124-128` | 195-228<br>124-128 | **BT/WiFi 双向互斥仍不完整**。BT 启用时会关闭 WiFi 并记录 `g_wifi_was_on`；但 `wifi_mgr_enable()` 完全不检查 BT 状态。用户在 BT 已启用时打开 WiFi，会导致两射频栈同时存在，极易触发 ESP32 堆内存不足或射频冲突崩溃。 | `wifi_mgr_enable()` 开头检查 `bt_mgr_is_enabled()`，若 BT 已启用则拒绝或先异步关闭 BT 并记录恢复标志；与 bt_manager 的 `g_wifi_was_on` 形成对称互斥。 | 缺少；新增 `test_wifi_bt_mutex.cpp` |

## P1 问题

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| APP-P1-01 | WiFi popup | `src/app/wifi/wifi_manager.cpp` | 105-114<br>131-145 | **popup 跨任务标志仍存在数据竞争**。`wifi_popup_request()` 用 spinlock 保护 `g_popup_content`/`g_popup_span`，但 `g_popup_active` 和 `g_popup_start` 在临界区外设置；UI 任务可能在标志已置位但内容/时长未写完时读取。`refresh()` 中读取 `g_popup_span` 也未加锁。 | 把 `g_popup_active`/`g_popup_start` 也纳入临界区；或将全部弹窗状态封装为一份由 `portMUX_TYPE` 保护的结构体。 | 缺少 `test_wifi_popup.cpp` |
| APP-P1-02 | 蓝牙管理器状态机 | `src/app/bluetooth/bt_manager.cpp` | 39-42<br>94-144 | **`BT_MGR_WARMUP` 为死状态**。状态枚举文档写 IDLE→WARMUP→ENABLED→CONNECTED，但 `bt_mgr_enable()` 直接设置 `g_state = BT_MGR_ENABLED`，`WARMUP` 从未使用，状态机文档与实际不符。 | 删除 `BT_MGR_WARMUP` 状态；或在 500ms 稳定延迟期间真正进入 `WARMUP` 再转到 `ENABLED`。 | 缺少 bt_manager 状态机测试 |
| APP-P1-03 | 蓝牙连接状态同步 | `src/app/bluetooth/bt_manager.cpp`<br>`src/app/bluetooth/bt_uart_service.cpp` | 224-240<br>79 | **`bt_mgr_update()` 跨任务读取 `g_connected` 无同步**。`bt_mgr_update()` 在 Xeros 内核任务中运行，`g_connected` 由 `bt_uart_poll()` 在 Arduino loop 任务中写入。变量非 `volatile`/atomic，存在可见性延迟与数据竞争。 | `g_connected`/`g_prev_connected` 改为 `volatile bool` 或 `std::atomic<bool>`；必要时加轻量同步。 | `test_ble_uart` 未覆盖并发状态 |
| APP-P1-04 | WiFi 扫描重复代码 | `src/app/wifi/wifi_manager.cpp` | 397-418<br>500-518 | **扫描启动代码重复**。扫描按钮回调与 WARMUP 后的自动扫描各自构造相同的 `wifi_scan_config_t` 并调用 `esp_wifi_scan_start()`，仅弹窗提示不同。 | 抽取 `static void wifi_mgr_scan_start(bool show_popup)` 统一处理错误码与弹窗。 | 缺少 WiFi 扫描测试 |
| APP-P1-05 | WiFi 扫描超时 | `src/app/wifi/wifi_manager.cpp` | 523-532 | **扫描超时未停止底层扫描**。超时后仅更新状态为 `SCAN_DONE` 并重建菜单，未调用 `esp_wifi_scan_stop()`。后台扫描可能继续占用射频，后续再次启动扫描可能失败。 | 超时分支增加 `esp_wifi_scan_stop()` 与 `g_scan_done = false` 清理。 | 同上 |
| APP-P1-06 | 串口输入状态机 | `src/app/serial_input/serial_input.cpp` | 40-46<br>63-67<br>159-161<br>184 | **配对码路径为死代码**。头文件声明 `WAITING_PAIR_CODE` / `PAIR_CODE_RECEIVED`，但实现中没有 `serial_request_pair_code()`，`g_target_addr` 定义后从未写入。状态分支理论上不可达，增加维护负担。 | 移除配对码相关死状态与 `g_target_addr`；或实现完整配对码请求接口并补测试。 | 缺少 `test_serial_input.cpp` |

## P2 问题

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| APP-P2-01 | taskmgr | `src/app/taskmgr/taskmgr_app.c` | 211<br>213 | **硬编码任务名识别 wifi-mgr/bt-mgr**。通过 `strcmp(t->name, "wifi-mgr")` / `"bt-mgr"` 判断；任务名一旦修改或本地化，清理逻辑失效。 | 在 `kern_task` 中增加 capability 标志（如 `KERN_TASK_FLAG_NET_STACK` / `BT_STACK`），taskmgr 按标志判断。 | 缺少 taskmgr 测试 |
| APP-P2-02 | settings/亮度 | `src/main.cpp`<br>`src/app/settings/settings.c` | 67<br>146-150 | **亮度转换在 main.cpp 重复计算**。`on_brightness_change_cb` 先算 `brightness = g_brightness_level * 10`，再调用 `settings_brightness_hw_value()` 内部又算一遍。 | 直接使用 `settings_brightness_hw_value()` 返回值，移除 main.cpp 中的 intermediate `brightness` 变量。 | `test_settings_accessors.cpp` 可扩展 |
| APP-P2-03 | app_menu/波特率 | `src/app/app_menu.c`<br>`src/app/settings/settings.c` | 50-51<br>163-170 | **波特率标签与 settings 映射表重复**。`build_baud_submenu()` 手写 `baud_labels` 与 `baud_levels`，与 `settings_serial_baud_hw_value()` 的 `s_baud_rate_table` 语义重复。 | 暴露 settings 的映射表或提供迭代接口，菜单直接遍历生成。 | `test_serial_baud.cpp` |
| APP-P2-04 | 串口监视器 | `src/app/serial_monitor/sm_app.cpp` | 214-226 | **BLE 模式每帧输出调试日志**。非 native 环境下每帧打印连接状态，严重影响性能和帧率。 | 用 `#if SM_DBG_ENABLED` 包裹这些 `Serial.printf`，或删除稳定后的调试代码。 | `test_serial_monitor.cpp` 不涉及 |
| APP-P2-05 | user_item 模板 | 多个 | - | **App 初始化模板不一致**。`about`、`token_usage` 不处理屏幕方向；`serial_monitor`、`oscilloscope`、`flasher` 进入横屏。缺少统一封装，样板代码重复。 | 在 `doc/tutorials/your-first-app.md` 中明确横屏需求；提供 `ui_service_user_item_enter/exit` 统一模板。 | `test_ui_service_landscape.cpp` |

## P3 问题

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| APP-P3-01 | WiFi 管理器 | `src/app/wifi/wifi_manager.cpp` | 60<br>361<br>363<br>472<br>564 | **`g_connecting_pass` 为死存储**。被多次写入但从未读取，增加代码噪音。 | 移除该变量或明确保留原因注释。 | 缺少 WiFi 测试 |
| APP-P3-02 | App 输入 | `src/app/app_input.c` | 85 | **检查 `bt_mgr_is_waiting_input()` 恒为 false**。Classic BT SPP 下该函数始终返回 false，分支冗余。 | 移除该分支或加注释说明保留用于未来 BLE 配对码输入。 | 现有输入测试未覆盖 |
| APP-P3-03 | settings | `src/app/settings/settings.c` | 197-213 | **弹簧参数越界默认值不一致**。`stiffness` 越界默认 5、`damping` 越界默认 9，与其他 setter 越界 clamp 到 min/max 的行为不一致。 | 统一为 clamp 到有效范围边界，并与 setter 行为一致。 | `test_settings_accessors.cpp` |
| APP-P3-04 | 全局状态引用 | `src/app/wifi/wifi_manager.cpp`<br>`src/app/bluetooth/bt_manager.cpp` | 49<br>77 | **仍用手写 `extern bool g_wifi_on`/`g_bt_on`**。上一轮 D14 未修复，应统一包含 `app_state.h`。 | 替换为 `#include "app/app_state.h"`。 | `test_app_state.cpp` |

---

## 本轮重构排期建议

| 子阶段 | 模块 | 处理的问题 ID |
|---|---|---|
| 11.1 | taskmgr / WiFi 管理器 | APP-P0-01、APP-P1-04、APP-P1-05、APP-P2-01 |
| 11.2 | 蓝牙管理器 / UART 服务 | APP-P0-02、APP-P0-03、APP-P1-02、APP-P1-03 |
| 11.3 | 串口输入 / 监视器 | APP-P1-06、APP-P2-04 |
| 11.4 | settings / app_menu | APP-P2-02、APP-P2-03、APP-P3-03、APP-P3-04 |
| 11.5 | user_item 模板与杂项 | APP-P2-05、APP-P3-01、APP-P3-02 |
| 11.6 | 文档 | 同步 `doc/app/`、`doc/tutorials/` 与代码变更 |

---

## 测试缺口

| 模块 | 已有测试 | 缺失测试 |
|---|---|---|
| WiFi 管理器 | 无 | `test_wifi_manager.cpp`（状态机、扫描、互斥） |
| 蓝牙管理器 | 无 | `test_bt_manager.cpp`（请求/状态机） |
| bt_uart_service | `test_ble_uart` | deinit 竞态、并发连接状态 |
| serial_input | 无 | `test_serial_input.cpp` |
| taskmgr | 无 | `test_taskmgr.cpp`（含 WiFi/BT 异步关闭） |
| settings | `test_settings_accessors.cpp`<br>`test_serial_baud.cpp` | 亮度/弹簧 hw_value 边界测试 |
