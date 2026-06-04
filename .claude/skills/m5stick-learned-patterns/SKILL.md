---
name: m5stick-learned-patterns
description: 25 project-specific instincts for M5Stick-P1 / ESP32 / Xerintosh UI — aggregated from session observations via continuous-learning-v2. Covers ESP32 platform traps, Xeros kernel architecture, embedded UI rendering, and debugging patterns.
---

# M5Stick-P1 Learned Patterns

Project-specific instincts aggregated from 25 session observations. Apply these patterns proactively when working on M5Stick-P1 code.

---

## 1. ESP32 Platform & Connectivity

### 1.1 BT: Arduino 框架预初始化 BT 栈 (confidence: 0.9)
**触发:** 初始化 ESP32 Arduino 蓝牙时
**规则:** 不要使用底层 ESP-IDF SPP API（`esp_spp_register_callback`, `esp_spp_init`）。Arduino 框架在 `setup()` 之前就初始化了 BT controller + Bluedroid，并注册了 SPP 回调。`esp_spp_register_callback()` 返回 `ESP_ERR_INVALID_STATE`(259)。`esp_bt_controller_get_status()` 返回过时的 0(IDLE)。
**正确做法:** 使用 `BluetoothSerial` 库——它内部正确检测并复用已初始化的栈。

### 1.2 BT: 延迟初始化以避免 OOM (confidence: 0.9)
**触发:** ESP32 同时启用 WiFi 和 BT 导致内存不足
**规则:** BT 初始化（~92KB via BluetoothSerial）必须延迟到 FreeRTOS 任务 spawn 之后。否则 `xTaskCreate` 全部失败。WiFi 默认关闭（`g_wifi_on=false`）释放 ~38KB。
**初始化顺序:** `M5.begin()` → UI init → 内核子系统 → spawn 任务 → BT init

### 1.3 看门狗: 阻塞调用 (confidence: 0.85)
**触发:** 在 ESP32 主/UI 任务中调用可能阻塞的函数
**规则:** ESP32 任务看门狗默认 5 秒超时。以下函数会阻塞：
- `NimBLEDevice::getScan()->start()` — 扫描期间阻塞
- `WiFi.scanNetworks(false)` — 同步扫描
- `delay()` / `hal_delay_ms()` — 阻塞调用任务
**解决方案:** ① 在独立 FreeRTOS 任务中运行 ② 使用异步变体（如 `WiFi.scanNetworks(true)`）③ 分块执行并 `vTaskDelay()` 让出 CPU

### 1.4 NimBLE 扫描必须异步 (confidence: 0.9)
**触发:** 使用 NimBLE 启动 BLE 扫描
**规则:** `NimBLEDevice::getScan()->start()` 是阻塞调用，在 UI 任务中会触发看门狗重启。必须在独立 FreeRTOS 任务中运行：
```c
static void scan_task(void *arg) {
    NimBLEDevice::getScan()->start(duration, false);
    vTaskDelete(NULL);
}
xTaskCreate(scan_task, "bt_scan", 4096, NULL, 1, &handle);
```

### 1.5 AXP192 关机 (confidence: 0.9)
**触发:** M5StickC 实现关机功能
**规则:** 电源按钮连接 AXP192 PMU，ESP32 无法检测。`esp_deep_sleep_start()` 只让 ESP32 休眠，AXP192 继续供电。必须用 `M5.Power.powerOff()`（写 AXP192 寄存器 0x32 bit7）。C 模块需要 `extern "C"` 包装：
```cpp
extern "C" void hal_power_off_hw(void) {
    M5.Display.sleep();
    M5.Power.powerOff();
}
```

### 1.6 C++ API 包装 (confidence: 0.85)
**触发:** C 代码需要调用 C++ API（M5Unified, NimBLE 等）
**规则:** 创建最小 `.cpp` 包装文件，用 `extern "C"` 函数桥接。用 `#ifndef NATIVE_TEST` 守卫硬件代码。

---

## 2. Xeros UI 框架

### 2.1 user_item 生命周期 (confidence: 0.9)
**触发:** 实现 `user_item->loop_function()`
**规则:** **不要**在 loop_function 内调用：
- `hal_display_clear()` — 框架在 `xerintosh_ui_main_core()` 前调用
- `hal_display_flush()` — 框架在 `xerintosh_ui_widget_core()` 后调用
- `hal_input_update()` — `app_input_process()` 在 loop 前调用
框架执行顺序：`clear → main_core(loop) → widget_core(popup) → flush`

### 2.2 动画范围陷阱 (confidence: 0.85)
**触发:** 用 `xerintosh_animation()` 动画化 [0, 1] 范围的值
**规则:** 函数有阈值 `fabs(*_pos - _pos_trg) <= 1.0f` 会直接跳转（无动画）。必须缩放到 [0, 100] 范围，渲染时再除回。

### 2.3 XOR 反色选中 (confidence: 0.85)
**触发:** 实现选中高亮或滑块需要反转文字颜色
**规则:** 用 `hal_draw_xor_rect(x, y, w, h)` — XOR 反转区域内所有像素，无需重绘文字或处理裁剪。

### 2.4 字体基线绘制 (confidence: 0.9)
**触发:** 调用 `hal_draw_string()`
**规则:** Y 坐标是文字**基线**（非左上角）。始终传 `y = row_top + font_height`。M5GFX 使用 `baseline_left` 对齐。

### 2.5 动态字体间距 (confidence: 0.85)
**触发:** 编写 UI 渲染代码
**规则:** 不要硬编码像素值。始终用 `hal_get_font_height()` 获取行高。中文用 `hal_get_cn_font()`，ASCII 用 `NULL`。颜色用 `COLOR_FG`(白) / `COLOR_BG`(黑)。

---

## 3. 弹窗与文本渲染

### 3.1 弹窗宽度钳位 (confidence: 0.85)
**触发:** 嵌入式弹窗宽度钳位
**规则:** 不能直接用 `SCREEN_WIDTH`，必须扣除边框层像素：`max_w = SCREEN_WIDTH - 8`（内框+外框）或 `SCREEN_WIDTH - 12`（留边距）。

### 3.2 多行文本换行 (confidence: 0.85)
**触发:** 小屏幕文本自动换行
**规则:** 支持 3 行回退：先尝试 2 行，若仍有超出则对第二段再次断行。字符宽度：ASCII ~7px，CJK ~12px。弹窗高度需随行数动态增加。

### 3.3 弹窗内容必须静态存储 (confidence: 0.9)
**触发:** 传递字符串给弹窗/info bar
**规则:** `content` 存储 `const char*` 指针，不复制字符串。**不能**传栈变量指针。必须用字符串字面量或 `static char[]` 缓冲区。

### 3.4 修复渲染而非内容 (confidence: 0.80)
**触发:** 用户反馈"文字超出渲染"
**规则:** 修复布局/渲染管线（换行、宽度、高度），**不要**缩短消息文本。用户期望完整内容正确显示。

### 3.5 退场动画覆盖弹窗 (confidence: 0.85)
**触发:** 退场动画期间需要显示弹窗
**规则:** `xerintosh_draw_exit_animation()` 在弹窗之后绘制全屏黑色遮罩。当弹窗需要在触发动画的状态下可见时，需跳过退场动画。

### 3.6 列表渲染裁剪 (confidence: 0.80)
**触发:** 渲染带 header/footer 的可滚动列表
**规则:** 用 `hal_set_clip_rect()` / `hal_clear_clip_rect()` 包裹列表项渲染，防止动画行溢出到 header/footer 区域。

### 3.7 滚动偏移动画 (confidence: 0.80)
**触发:** `xerintosh_anim_row_list_t` 滚动偏移变化
**规则:** 在调用 `xerintosh_anim_row_list_refresh()` 之前，将 scroll delta 注入所有可见行的 current Y。refresh 设置 `y_trg`，差值驱动平滑滑动动画。

---

## 4. Xeros 内核架构

### 4.1 FreeRTOS API 隔离 (confidence: 0.85)
**触发:** 添加调度器或线程代码到内核
**规则:** 所有 FreeRTOS API 调用必须限制在 `src/kernel/kern_port.c`。内核文件不能直接 include `<freertos/*.h>`。使用 `kern_port.h` API：`kern_port_thread_spawn()`, `kern_port_task_yield()`, `kern_port_task_exit()`, `kern_port_switch_to()`。

### 4.2 双语保护任务名 (confidence: 0.8)
**触发:** 添加 `kern_task_is_protected()` 任务名
**规则:** 同时包含英文和中文名。虚拟任务（user_item 注册）用 `base_item.content` 作为任务名，可能是中文：
```c
static const char *protected_names[] = {
    "idle", "shell", "ui", "taskmgr", "任务管理器", NULL
};
```

---

## 5. 调试模式

### 5.1 按钮隔离需独立计时 (confidence: 0.85)
**触发:** 实现多按钮手势检测（如 A+B 双键）
**规则:** 手势检测器必须自己管理计时，不能依赖 `hal_input_get_press_duration()`（该值由 `hal_input_simple_process()` 更新，按钮事件被阻塞时不会调用）。

### 5.2 静态缓冲区 strcmp 陷阱 (confidence: 0.9)
**触发:** 用 `strcmp` 检测存储指针的"相同内容"
**规则:** 当调用方用 `static char[]` 每帧覆写时，存储指针与新指针指向同一地址。`strcmp` 比较已被覆写的缓冲区，始终返回 0。**修复:** 先判等指针 `_content != stored_ptr`，再 strcmp。

### 5.3 状态机守卫死锁 (confidence: 0.9)
**触发:** 设计带守卫标志的异步状态机
**规则:** 守卫变量只能由状态机自身设置，不能由回调设置。回调设置守卫会导致状态机的检查永远为 false，造成死锁。

### 5.4 非交互终端串口读取 (confidence: 0.8)
**触发:** `pio device monitor` 在 agent/CI 环境失败
**规则:** 用 Python pyserial + RTS reset 捕获启动日志：
```python
ser = serial.Serial(port, 115200, timeout=0.5, exclusive=True)
ser.setRTS(True); time.sleep(0.2); ser.setRTS(False)
ser.reset_input_buffer()
```
