# Classic Bluetooth SPP 重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将当前基于 NimBLE/BLE 的蓝牙框架彻底替换为 ESP32 Arduino 内置的 Classic Bluetooth SPP (`BluetoothSerial`)，并适配所有 UI 引用。

**Architecture:** 保持 `bt_uart_service.h` 的 C API 接口不变（向后兼容），底层从 NimBLE NUS 替换为 `BluetoothSerial`。`bt_manager` 从 BLE 扫描/广告模式切换为 Classic BT 设备发现/配对模式。UI 层面的文本从 "BLE" 统一改为 "蓝牙"。

**Tech Stack:** ESP32 Arduino, `BluetoothSerial` (内置库), PlatformIO, GoogleTest

**理解说明：** 用户要求 "classic BLE 协议"，经过分析，这最可能指的是 **Classic Bluetooth SPP (Serial Port Profile)**。原因：
1. ESP32 Arduino 框架内置 `BluetoothSerial` 库，无需额外依赖
2. SPP 提供类似 `Serial` 的流式 API，与当前无线串口应用场景完美匹配
3. 不需要 BLE 复杂的广告/扫描/NUS 服务机制
4. 没有 BLE 的 MTU 限制，吞吐量更高
5. 当前代码中的蓝牙功能本质就是 "无线串口"，SPP 是此场景的行业标准

---

## 文件结构变更

| 文件 | 动作 | 说明 |
|------|------|------|
| `platformio.ini` | 修改 | 移除 `h2zero/NimBLE-Arduino` 依赖 |
| `src/app/bluetooth/bt_uart_service.cpp` | 重写 | NimBLE NUS → `BluetoothSerial` 封装 |
| `src/app/bluetooth/bt_uart_service.h` | 保持 | API 不变，仅更新注释中的协议描述 |
| `src/app/bluetooth/bt_manager.cpp` | 重写 | NimBLE 扫描/配对 → Classic BT 发现/配对 |
| `src/app/bluetooth/bt_manager.h` | 保持 | API 不变，仅更新状态枚举注释 |
| `src/app/ble_serial/ble_serial.cpp` | 修改 | UI 文本："BLE Serial" → "BT Serial" |
| `src/app/ble_serial/ble_serial.h` | 保持 | API 不变 |
| `src/app/app_init.c` | 修改 | 菜单文本："BLE 串口" → "蓝牙串口" |
| `test/test_ble_uart.cpp` | 修改 | 测试适配（如果 API 不变则只需更新注释） |
| `src/native_main.cpp` | 确认 | 检查是否有蓝牙相关引用需要更新 |

---

## Task 1: 移除 NimBLE 依赖并确认 BluetoothSerial 可用

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: 从 `platformio.ini` 移除 NimBLE-Arduino 依赖**

  将：
  ```ini
  lib_deps =
      m5stack/M5Unified@^0.2.14
      m5stack/M5GFX@^0.2.20
      h2zero/NimBLE-Arduino@^1.4.0
  ```
  改为：
  ```ini
  lib_deps =
      m5stack/M5Unified@^0.2.14
      m5stack/M5GFX@^0.2.20
  ```

- [ ] **Step 2: 编译验证 BluetoothSerial 可用**

  运行：`pio run -e m5stick-c`
  预期：成功编译（`BluetoothSerial` 是 ESP32 Arduino 框架内置库，不需要显式声明依赖）

- [ ] **Step 3: Commit**

  ```bash
  git add platformio.ini
  git commit -m "chore: remove NimBLE-Arduino dependency, switch to built-in BluetoothSerial"
  ```

---

## Task 2: 重写 `bt_uart_service.cpp`（硬件实现）

**Files:**
- Modify: `src/app/bluetooth/bt_uart_service.cpp`
- Modify: `src/app/bluetooth/bt_uart_service.h`（更新注释）

**设计要点：**
- 保持 `bt_uart_service.h` 中的 C API 完全不变（`bt_uart_service_init`, `bt_uart_send`, `bt_uart_set_rx_callback`, 等）
- 硬件实现部分（`#else` 分支）从 NimBLE NUS 改为 `BluetoothSerial`
- `BluetoothSerial` 的 API：`begin(name)`, `available()`, `read()`, `write(data, len)`, `connected()`, `end()`
- 需要非阻塞轮询方式：每帧检查 `available()` 读取数据，触发 RX 回调
- 发送：`BluetoothSerial::write()` 直接发送，不需要 MTU 分块
- 连接状态：`BluetoothSerial::connected()` 返回布尔值
- 不再 notify/广告，SPP 使用 RFCOMM 通道，客户端通过蓝牙设置中的 "配对" 来连接

- [ ] **Step 1: 更新 `bt_uart_service.h` 注释**

  将文件头部的注释从 "BLE UART 服务" 改为 "蓝牙串口服务 (Classic Bluetooth SPP)"。

- [ ] **Step 2: 重写硬件实现分支**

  替换 `#else` 分支（NimBLE 实现）为 `BluetoothSerial` 实现：

  ```cpp
  #include <BluetoothSerial.h>

  static BluetoothSerial g_bt_serial;
  static bool g_initialized = false;

  /* 状态轮询变量 */
  static bool g_prev_connected = false;

  bool bt_uart_service_init(void) {
      ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
      ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
      g_connected = false;
      g_prev_connected = false;
      g_initialized = false;
      g_rx_cb = NULL;
      g_conn_cb = NULL;

      g_bt_serial.begin("M5Stick-P1");  /* 设备名称 */
      g_initialized = true;
      return true;
  }

  void bt_uart_service_deinit(void) {
      g_bt_serial.end();
      g_connected = false;
      g_prev_connected = false;
      g_initialized = false;
      g_rx_cb = NULL;
      g_conn_cb = NULL;
  }

  /* 非阻塞轮询：应在主循环中每帧调用 */
  static void bt_uart_poll(void) {
      if (!g_initialized) return;

      /* 检查连接状态变化 */
      bool now_connected = g_bt_serial.connected();
      if (now_connected != g_prev_connected) {
          g_prev_connected = now_connected;
          g_connected = now_connected;
          if (g_conn_cb) g_conn_cb(now_connected);
      }

      /* 读取可用数据 */
      while (g_bt_serial.available() > 0) {
          uint8_t buf[64];
          int len = g_bt_serial.readBytes(buf, sizeof(buf));
          if (len > 0) {
              ringbuf_write(&g_rx_buf, buf, (uint16_t)len);
              if (g_rx_cb) g_rx_cb(buf, (uint16_t)len);
          }
      }
  }

  uint16_t bt_uart_send(const uint8_t *data, uint16_t len) {
      if (!data || len == 0 || !g_connected || !g_initialized) return 0;
      return (uint16_t)g_bt_serial.write(data, len);
  }

  /* ... 其余函数保持不变（g_connected 由 poll 更新） ... */
  ```

  **注意：** `bt_uart_poll()` 需要在某处被调用。考虑两种方案：
  - 方案 A：在 `bt_mgr_update()` 中调用（BT Manager 负责轮询）
  - 方案 B：在 `bt_uart_service.h` 中新增 `bt_uart_poll()` 公开 API，由 `bt_mgr_update()` 调用

  推荐 **方案 A**：在 `bt_mgr_update()` 内部调用 `bt_uart_poll()`，保持 `bt_uart_service.h` API 不变。

- [ ] **Step 3: 硬件编译验证**

  运行：`pio run -e m5stick-c`
  预期：编译成功

- [ ] **Step 4: Commit**

  ```bash
  git add src/app/bluetooth/bt_uart_service.cpp src/app/bluetooth/bt_uart_service.h
  git commit -m "refactor: replace NimBLE NUS with BluetoothSerial SPP"
  ```

---

## Task 3: 重写 `bt_manager.cpp`（硬件实现）

**Files:**
- Modify: `src/app/bluetooth/bt_manager.cpp`

**设计要点：**
- Classic Bluetooth SPP 的工作方式与 BLE 完全不同：
  - 不需要 BLE 扫描/广告
  - 设备通过系统蓝牙设置进行配对
  - SPP 服务器启动后等待客户端连接
  - 不需要 "配对码" 输入流程（配对在系统层面完成）
- 状态机简化：
  - `IDLE` → `ENABLED`（启用 BluetoothSerial）
  - `ENABLED` → `CONNECTED`（有客户端连接）
  - `CONNECTED` → `ENABLED`（客户端断开）
- 不再需要：扫描结果、扫描任务、配对码输入、设备列表动态构建
- 菜单简化：设置中保留 "蓝牙" 开关，但不再需要子菜单（扫描结果列表）
- 已保存设备列表也不再需要（Classic BT 的配对信息由系统管理）

- [ ] **Step 1: 简化状态枚举**

  `bt_manager.h` 中的 `bt_mgr_state_t` 需要更新为更简单的状态：

  ```c
  typedef enum {
      BT_MGR_IDLE,       /* 空闲/关闭 */
      BT_MGR_WARMUP,     /* 预热中（启动 BluetoothSerial） */
      BT_MGR_ENABLED,    /* 已启用，等待连接 */
      BT_MGR_CONNECTED,  /* 有客户端连接 */
  } bt_mgr_state_t;
  ```

  **注意：** 修改头文件后，需要检查 `app_init.c` 中是否有使用旧状态的代码（如 `BT_MGR_PAIRING`）。实际上 `app_init.c` 只调用 `bt_mgr_is_waiting_input()`，不直接引用状态枚举。

- [ ] **Step 2: 重写硬件实现分支**

  替换 `#else` 分支：

  ```cpp
  #include <BluetoothSerial.h>
  #include "app/bluetooth/bt_uart_service.h"

  extern bool g_bt_on;

  static bool g_bt_enabled = false;
  static bt_mgr_state_t g_state = BT_MGR_IDLE;
  static unsigned long g_warmup_start_time = 0;
  #define BT_WARMUP_DELAY_MS 1500

  /* UI 菜单指针 */
  static xerintosh_list_item_t *g_settings_list = NULL;

  void bt_mgr_init(void) {
      g_bt_enabled = false;
      g_state = BT_MGR_IDLE;

      xerintosh_list_item_t *root = xerintosh_get_root_list();
      if (root && root->child_num > 0) {
          g_settings_list = root->child_list_item[0];  /* "设置" */
      }
  }

  void bt_mgr_enable(void) {
      Serial.println("[BT] bt_mgr_enable called");
      g_bt_enabled = true;
      g_warmup_start_time = millis();
      g_state = BT_MGR_WARMUP;
      Serial.println("[BT] bt_mgr_enable done");
  }

  void bt_mgr_disable(void) {
      bt_uart_service_deinit();
      g_bt_enabled = false;
      g_state = BT_MGR_IDLE;
  }

  bool bt_mgr_is_waiting_input(void) {
      /* Classic BT SPP 不需要串口输入配对码 */
      return false;
  }

  void bt_mgr_update(void) {
      if (!g_bt_enabled && g_state == BT_MGR_IDLE) return;

      static bt_mgr_state_t last_state = BT_MGR_IDLE;
      if (g_state != last_state) {
          Serial.printf("[BT] State: %d -> %d\n", last_state, g_state);
          last_state = g_state;
      }

      switch (g_state) {
      case BT_MGR_WARMUP: {
          if (millis() - g_warmup_start_time >= BT_WARMUP_DELAY_MS) {
              if (bt_uart_service_init()) {
                  g_state = BT_MGR_ENABLED;
              } else {
                  g_state = BT_MGR_IDLE;
                  ui_svc_notify_error("蓝牙启动失败");
              }
          }
          break;
      }
      case BT_MGR_ENABLED:
      case BT_MGR_CONNECTED: {
          /* 轮询 BluetoothSerial，更新连接状态 */
          bt_uart_poll();  /* 内部调用，由 bt_uart_service.cpp 提供 */
          bool connected = bt_uart_is_connected();
          g_state = connected ? BT_MGR_CONNECTED : BT_MGR_ENABLED;
          break;
      }
      default:
          break;
      }
  }

  void bt_mgr_on_switch_toggle(void *ud) {
      svc_mgr_handle_switch_toggle(&g_bt_on, bt_mgr_enable, bt_mgr_disable, ud);
  }

  extern "C" void bt_mgr_task_main(void *arg) {
      (void)arg;
      kern_poll_loop(bt_mgr_update, 50);
  }
  ```

  **注意：** `bt_uart_poll()` 需要在 `bt_uart_service.cpp` 中声明为 `extern "C"` 或 `extern` 供 `bt_manager.cpp` 调用。或者更好的方案：将 `bt_uart_poll()` 封装在 `bt_uart_service.h` 中作为一个新 API（仅在非 NATIVE_TEST 时可用）。

  更干净的方案：在 `bt_uart_service.h` 中新增 `void bt_uart_poll(void);` 声明，在 NATIVE_TEST 中为空实现。

- [ ] **Step 3: 在 `bt_uart_service.h` 中添加 `bt_uart_poll()` 声明**

  ```c
  /**
   * @brief 非阻塞轮询 BluetoothSerial 数据（Classic BT SPP）
   * @note  应在主循环中每帧调用，读取可用数据并触发回调
   */
  void bt_uart_poll(void);
  ```

  NATIVE_TEST 桩实现：
  ```cpp
  void bt_uart_poll(void) {}
  ```

- [ ] **Step 4: 清理不再需要的代码**

  删除 `bt_manager.cpp` 中所有以下代码：
  - `BleDeviceResult` 结构体
  - `g_scan_results`, `g_scan_result_count`, `g_scan_start_time`
  - `g_scan_task_running`, `g_scan_task_handle`
  - `g_devices_list`
  - `ScanCallbacks` 类
  - `bt_scan_task()` FreeRTOS 任务
  - `on_device_button_pressed()`, `on_bt_reconnect_pressed()`, `on_bt_delete_pressed()`, `on_bt_scan_pressed()`
  - `rebuild_device_list()`

- [ ] **Step 5: 硬件编译验证**

  运行：`pio run -e m5stick-c`
  预期：编译成功

- [ ] **Step 6: Commit**

  ```bash
  git add src/app/bluetooth/bt_manager.cpp src/app/bluetooth/bt_manager.h src/app/bluetooth/bt_uart_service.cpp src/app/bluetooth/bt_uart_service.h
  git commit -m "refactor: simplify bt_manager for Classic Bluetooth SPP"
  ```

---

## Task 4: 适配 UI 文本（ble_serial + app_init）

**Files:**
- Modify: `src/app/ble_serial/ble_serial.cpp`
- Modify: `src/app/ble_serial/ble_serial.h`（更新注释）
- Modify: `src/app/app_init.c`

- [ ] **Step 1: 修改 `ble_serial.cpp` 中的标题文本**

  ```cpp
  /* 修改 draw_header() 中的标题 */
  int16_t title_w = hal_get_string_width("BT Serial");
  hal_draw_string(title_x, title_y, "BT Serial", COLOR_FG);
  ```

  所有 "BLE Serial" 改为 "BT Serial"。

- [ ] **Step 2: 修改 `app_init.c` 中的菜单文本**

  ```c
  xerintosh_list_item_t* item5 = xerintosh_new_user_item(
      "蓝牙串口", ble_serial_init, ble_serial_loop, ble_serial_exit, default_icon);
  ```

  所有 "BLE 串口" 改为 "蓝牙串口"。

- [ ] **Step 3: 检查其他文件中是否有 "BLE" 文本**

  运行：`grep -r "BLE" src/app/ --include="*.c" --include="*.cpp" --include="*.h"`
  预期：确认所有相关引用都已更新。

- [ ] **Step 4: Commit**

  ```bash
  git add src/app/ble_serial/ble_serial.cpp src/app/ble_serial/ble_serial.h src/app/app_init.c
  git commit -m "refactor: rename BLE references to BT/Bluetooth in UI"
  ```

---

## Task 5: 更新 Native 测试

**Files:**
- Modify: `test/test_ble_uart.cpp`
- Modify: `test/test_native.cpp`（如有蓝牙相关引用）

**设计要点：**
- `bt_uart_service.h` 的 API 保持不变，所以 `test_ble_uart.cpp` 的核心测试逻辑不需要大改
- 需要确保 `bt_uart_poll()` 的 NATIVE_TEST 桩实现可用
- 测试中的注释需要更新，移除 NimBLE 引用

- [ ] **Step 1: 更新 `test/test_ble_uart.cpp` 注释**

  将测试文件头部的 "BLE UART" 注释改为 "Bluetooth UART (Classic SPP)"。

- [ ] **Step 2: 确保 native 编译通过**

  运行：`pio test -e native`
  预期：`test_ble_uart` 全部通过。`test_native` 的 SIGSEGV 是既有问题，与本次修改无关。

- [ ] **Step 3: Commit**

  ```bash
  git add test/
  git commit -m "test: update BT test comments for Classic SPP"
  ```

---

## Task 6: 最终验证

- [ ] **Step 1: 完整硬件编译**

  运行：`pio run -e m5stick-c`
  预期：编译成功，无 NimBLE 相关警告/错误

- [ ] **Step 2: 完整 Native 测试**

  运行：`pio test -e native`
  预期：`test_ble_uart` 全部通过

- [ ] **Step 3: 代码审查自查**

  - [ ] 确认 `platformio.ini` 中已移除 `h2zero/NimBLE-Arduino`
  - [ ] 确认没有遗留的 `#include <NimBLEDevice.h>`
  - [ ] 确认没有遗留的 `NimBLE` 类引用
  - [ ] 确认 `bt_uart_service.h` API 未变（向后兼容）
  - [ ] 确认 UI 文本已全部更新
  - [ ] 确认 `.pio/build/` 缓存已清理（`rm -rf .pio/build/` 后重新编译）

- [ ] **Step 4: Commit**

  ```bash
  git commit -m "feat: complete Bluetooth Classic SPP migration"
  ```

---

## 附录：BluetoothSerial API 参考

ESP32 Arduino 框架中的 `BluetoothSerial` 关键 API：

```cpp
#include <BluetoothSerial.h>

BluetoothSerial serial;

serial.begin("设备名称");           // 启动 SPP 服务器
serial.end();                        // 关闭
serial.write(data, len);             // 发送数据
serial.available();                  // 可读字节数
serial.read();                       // 读取一个字节
serial.readBytes(buf, len);          // 读取多个字节
serial.connected();                  // 是否有客户端连接
serial.println("text");              // 发送字符串 + 换行
serial.setPin(pin);                  // 设置配对 PIN 码（可选）
serial.enableSSP();                  // 启用安全简单配对（SSP）
```

**配对流程：** Classic Bluetooth 的配对由客户端（手机/电脑）在系统蓝牙设置中完成，不需要应用层处理配对码输入。如果需要 PIN 码配对，可以调用 `serial.setPin("1234")`。

---

## Spec Coverage 自查

| 用户需求 | 对应 Task |
|----------|-----------|
| 彻底卸载当前蓝牙框架 | Task 1 (移除 NimBLE 依赖), Task 2 (重写 bt_uart_service), Task 3 (重写 bt_manager) |
| 换成支持 classic BLE 的协议 | Task 2 (BluetoothSerial SPP), Task 3 (Classic BT 状态机) |
| 修改 UI 中蓝牙相关部分 | Task 4 (UI 文本适配) |
| 保持测试可用 | Task 5 (Native 测试更新) |
