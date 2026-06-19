# 重构报告：App 层（第十一轮 · 2026-06-19）

## 范围

`src/app/settings/*`、`src/app/app_menu.c`、`src/main.cpp`、`src/app/app_input.c`、`src/app/bluetooth/*`、`src/app/wifi/wifi_manager.cpp`、`src/app/serial_monitor/sm_app.cpp`、相关单元测试。

## 目标

- 消除 App 层重复计算与重复映射表。
- 统一越界处理行为，减少维护负担。
- 降低蓝牙连接状态跨任务可见性风险。
- 减少串口监视器每帧调试日志对性能的影响。
- 用 `app/app_state.h` 替代手写 `extern bool g_wifi_on/g_bt_on`。

## 处理的问题清单

| ID | 优先级 | 文件 | 修复内容 |
|----|--------|------|----------|
| APP-P2-02 | P2 | `src/main.cpp` | 移除 `on_brightness_change_cb()` 中 `brightness = g_brightness_level * 10` 的重复计算，直接令 `brightness = settings_brightness_hw_value()` 并存储档位；`setup()` 中同步缓存 HW 值，使 sysfs 初始值语义与 UI/storage 一致。 |
| APP-P2-03 | P2 | `src/app/settings/settings.c/h`, `src/app/app_menu.c` | 在 settings 中暴露 `settings_serial_baud_count()` 与 `settings_serial_baud_table()`；`build_baud_submenu()` 直接遍历映射表生成按钮，删除手写的 `baud_labels`/`baud_levels` 数组。 |
| APP-P2-04 | P2 | `src/app/serial_monitor/sm_app.cpp` | 新增 `SM_DBG_ENABLED` 开关（默认 0），包裹所有 `[SM-DBG]` 串口日志；BLE 模式下不再每帧输出调试信息。 |
| APP-P1-03 | P1 | `src/app/bluetooth/bt_uart_service.cpp` | `g_connected` 与 `g_prev_connected` 改为 `volatile bool`，缓解 `bt_uart_poll()`（Arduino loop 任务）写入、`bt_mgr_update()`（内核任务）读取时的可见性延迟。 |
| APP-P3-03 | P3 | `src/app/settings/settings.c` | `settings_spring_stiffness_hw_value()` / `settings_spring_damping_hw_value()` 对越界值统一 clamp 到 `[1, 10]`，与 setter 行为保持一致（原实现分别默认回退到 5/9）。 |
| APP-P3-04 | P3 | `src/app/wifi/wifi_manager.cpp`, `src/app/bluetooth/bt_manager.cpp` | 删除手写 `extern bool g_wifi_on;` / `extern bool g_bt_on;`，统一 `#include "app/app_state.h"`。 |
| APP-P3-02 | P3 | `src/app/app_input.c` | 移除 `bt_mgr_is_waiting_input()` 冗余分支（Classic BT SPP 下恒为 false），保留注释说明。 |

## 新增 / 修改的 Public API

| API | 文件 | 说明 |
|-----|------|------|
| `int settings_serial_baud_count(void)` | `settings.h` | 返回波特率档位总数（当前为 6）。 |
| `const int32_t *settings_serial_baud_table(void)` | `settings.h` | 返回波特率映射表只读指针，长度由 `settings_serial_baud_count()` 给出。 |

## 未在本次处理的问题

以下问题涉及较大范围的状态机/线程模型改动，已记录在原诊断报告中，留待后续阶段专门处理：

- **APP-P0-01 / APP-P2-01**：taskmgr 中仍通过任务名判断 WiFi/BT 任务；长期应引入 `kern_task` capability 标志并完全异步化。
- **APP-P0-02**：`bt_uart_service_deinit()` 虽已使用信号量，但复杂的 Bluedroid 生命周期仍需更系统的测试覆盖。
- **APP-P0-03 / APP-P1-05**：WiFi/BT 双向互斥基础逻辑已存在，但 `wifi_mgr_enable()` 与扫描超时清理仍可进一步硬化。
- **APP-P1-04 / APP-P1-05**：WiFi 扫描启动代码重复及扫描超时未调用 `esp_wifi_scan_stop()`。
- **APP-P1-06**：`serial_input` 中配对码相关死代码。
- **APP-P2-05**：`user_item` 进入/退出横屏模板未统一封装。

## 验证

- 硬件构建：`pio run -e m5stick-c` ✅ SUCCESS。
- Native 测试：`pio test -e native --filter test_native` 共 187 个测试用例通过；套件退出时的 `SIGTRAP` 为已知的 teardown 信号，与本次修改无关。
- 新增测试：
  - `test_settings_accessors.cpp`：弹簧硬度/阻尼有效值映射、越界 clamp。
  - `test_serial_baud.cpp`：波特率映射表访问器与 `settings_serial_baud_hw_value()` 语义一致。

## 变更文件列表

| 文件 | 变更类型 |
|------|----------|
| `src/app/settings/settings.c` | 弹簧 clamp、波特率表访问器 |
| `src/app/settings/settings.h` | 新增 API 声明 |
| `src/app/app_menu.c` | 使用波特率表构建子菜单 |
| `src/main.cpp` | 亮度去重、缓存 HW 值 |
| `src/app/app_input.c` | 移除冗余 BT 输入分支 |
| `src/app/bluetooth/bt_uart_service.cpp` | volatile 连接状态 |
| `src/app/bluetooth/bt_manager.cpp` | 统一 include `app_state.h` |
| `src/app/wifi/wifi_manager.cpp` | 统一 include `app_state.h` |
| `src/app/serial_monitor/sm_app.cpp` | 调试日志开关 |
| `test/test_native/test_settings_accessors.cpp` | 弹簧测试 |
| `test/test_native/test_serial_baud.cpp` | 波特率表测试 |
| `doc/app/settings.md` | 同步新增 API 与弹簧 clamp 说明 |

## 后续建议

1. 在 `kern_task` 中增加 capability 标志（`KERN_TASK_FLAG_NET_STACK` / `KERN_TASK_FLAG_BT_STACK`），替换 taskmgr 的字符串匹配。
2. 补充 `test_wifi_manager.cpp`、`test_bt_manager.cpp`、`test_serial_input.cpp` 等缺失的测试。
3. 抽取 `user_item` 横屏进入/退出模板到 `ui_service_user_item_enter/exit`，减少各 App 样板代码。
