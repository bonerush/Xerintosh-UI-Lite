# Plan: 完全解耦 FreeRTOS — Xeros 原生内核迁移

**创建日期**: 2026-05-29
**状态**: 📋 待审批
**目标**: 将 M5Stick-P1 从 FreeRTOS 完全解耦，Xeros 内核成为唯一应用级调度器

---

## 前提与约束（必读）

### 硬件现实

ESP32 芯片的底层固件（bootloader、ESP-IDF 组件）**深度绑定 FreeRTOS**：

| 组件 | FreeRTOS 依赖 | 可替代性 |
|------|-------------|---------|
| ESP-IDF bootloader | `xTaskCreate` 启动第一个任务 | ❌ 需自行编写 2nd-stage bootloader |
| WiFi 协议栈 (`esp_wifi`) | 内部创建 FreeRTOS 事件任务 | ❌ 需移植整个 WiFi 协议栈 |
| NimBLE 蓝牙栈 | 完整 FreeRTOS NPL 移植层 | ❌ 需移植 NimBLE 到裸机 |
| M5Unified (IMU 驱动) | `vTaskDelay`, `xSemaphore` | ⚠️ 可自行编写 I2C 驱动 |
| M5GFX (显示驱动) | `xSemaphoreCreateMutex` | ⚠️ 可自行编写 SPI 驱动 |
| Arduino 框架 | `xTaskCreateUniversal(loopTask)` | ✅ 替换为 ESP-IDF `app_main` |

### 分层策略

采用**三层解耦**策略，而非一步到位：

```
┌─────────────────────────────────────────┐
│  Tier 3: 应用层                          │
│  Xeros 原生调度器管理所有 App 任务       │  ← 完全无 FreeRTOS
│  (UI, TaskMgr, SerialMonitor, Shell)    │
├─────────────────────────────────────────┤
│  Tier 2: 驱动层                          │
│  自研裸机驱动替代 Arduino 库             │  ← 完全无 FreeRTOS
│  (ST7789 SPI, MPU6886 I2C, GPIO)       │
├─────────────────────────────────────────┤
│  Tier 1: 系统服务层（黑盒）              │
│  ESP-IDF WiFi / NimBLE / 中断管理       │  ← FreeRTOS 存在但不可见
│  (作为不可修改的系统固件对待)            │
└─────────────────────────────────────────┘
```

**Tier 1 不修改**：WiFi/NimBLE 内部使用 FreeRTOS 是 ESP32 的硬件事实，强行替换得不偿失——就好比在 STM32 上重写 USB 协议栈。我们接受它们作为"黑盒系统服务"，但不让应用代码接触 FreeRTOS API。

---

## Step 1: 修复 XEROS_NATIVE_SCHED 上下文切换

**目标**: `kern_ctx_init` 的 Xtensa 内联汇编正确工作，不触发 WDT

**上下文摘要**:
当前 `kern_ctx_esp32.h:70-97` 的 `callx8` 内联汇编存在严重问题：在 Xtensa 窗口寄存器架构上，`callx8` 会旋转寄存器窗口，导致 `setjmp` 保存/恢复的寄存器与 `kern_ctx_init` 的调用者不匹配。当入口函数返回时，返回地址指向错误位置，触发 WDT。

**任务列表**:
- [ ] 1.1 研究 Xtensa ISA 窗口寄存器机制（a0-a15 窗口化，每次 call 旋转 8 个寄存器）
- [ ] 1.2 将所有任务调度代码放在**平坦寄存器窗口**内执行（确保 call depth < 8）
- [ ] 1.3 重写 `kern_ctx_init`：使用 `setjmp` + 手动 `mov a1` 切换栈，避免 `callx8`
- [ ] 1.4 添加 `kern_ctx_switch_to`：调度器保存自身上下文后 `longjmp` 到目标任务
- [ ] 1.5 添加 `kern_ctx_yield`：任务 `setjmp` 保存自身后 `longjmp` 回调度器
- [ ] 1.6 添加 `kern_port_idle`：用 `esp_timer` 或忙等待替代 `vTaskDelay`
- [ ] 1.7 编写纯 C 单元测试（无硬件）：模拟 setjmp/longjmp 上下文切换

**验证**:
```bash
# 1. 添加 -D XEROS_NATIVE_SCHED 到 platformio.ini
# 2. 烧录并观察串口：
#    [  OK  ] port: native scheduler (setjmp/longjmp) initialized
#    [  OK  ] UI task spawned (pid=1)
#    [  OK  ] Kernel boot complete, entering scheduler loop
#    [LOOP] heartbeat, free_heap=...
# 3. 屏幕显示 boot logo 并进入菜单
# 4. 运行 pio test -e native（确保回归测试通过）
```

**回滚**: 移除 `-D XEROS_NATIVE_SCHED` 恢复 FreeRTOS 包装器

**预计改动文件**:
- `src/kernel/kern_ctx_esp32.h` — 重写上下文切换汇编
- `src/kernel/kern_port.c` — 已有 XEROS_NATIVE_SCHED 桩，可能需要微调
- `src/kernel/kern_port_native.c` — 检查 XEROS_NATIVE_SCHED 路径是否启用
- `platformio.ini` — 添加 `-D XEROS_NATIVE_SCHED`

---

## Step 2: 消除项目源码中的剩余 FreeRTOS 调用

**目标**: 项目源码零 FreeRTOS API 引用（`kern_port.c` 除外）

**上下文摘要**:
当前有 ~9 处 FreeRTOS API 分布在 2 个非端口文件中：

| 文件 | API | 替代方案 |
|------|-----|---------|
| `dev_ttyS0.cpp:32,38` | `portMUX_TYPE`, `portMUX_INITIALIZER_UNLOCKED` | `std::atomic<int>` 或 `volatile int` + 禁用中断 |
| `dev_ttyS0.cpp:61,63,89,91,133,135,147,149` | `portENTER_CRITICAL` / `portEXIT_CRITICAL` | 同上 |
| `kern_shell_cmds.c:617` | `vTaskDelay(pdMS_TO_TICKS(2000))` | `kern_sleep_ms(2000)` 或忙等待 |

**任务列表**:
- [ ] 2.1 `dev_ttyS0.cpp`：用 `portDISABLE_INTERRUPTS()` / `portENABLE_INTERRUPTS()` 或 `std::atomic` 替代 `portMUX_TYPE`
- [ ] 2.2 `dev_ttyS0.cpp`：移除 `#include <Arduino.h>` 中的隐式 FreeRTOS 依赖，改用 ESP-IDF `driver/uart.h`
- [ ] 2.3 `kern_shell_cmds.c`：`vTaskDelay` → 移除（top 命令改为 `kern_sleep_ms` 或单次快照模式）
- [ ] 2.4 搜索所有源码确认零 FreeRTOS 引用：`grep -r "freertos\|FreeRTOS\|vTask\|xTask\|xSemaphore\|xQueue\|portMUX\|pdMS_TO_TICKS\|portMAX_DELAY" src/`

**验证**:
```bash
grep -rn "freertos\|FreeRTOS\|vTask\|xTask\|xSemaphore\|portMUX\|pdMS\|portMAX" src/ | grep -v kern_port.c | grep -v "//\|/\*"
# 预期输出：空（kern_port.c 除外）
```

**回滚**: 每个文件可独立回退

**预计改动文件**:
- `src/kernel/devices/dev_ttyS0.cpp` — 临界区替代
- `src/kernel/kern_shell_cmds.c` — vTaskDelay 替代

---

## Step 3: 替换 Arduino 框架为 ESP-IDF

**目标**: `framework = espidf`，移除所有 Arduino API 依赖

**上下文摘要**:
Arduino 框架在 ESP32 上由 FreeRTOS 任务启动 `loop()`。替换为 ESP-IDF 后，我们在 `app_main()` 中自行初始化并运行 Xeros 调度器。这是工作量最大的一步，因为几乎所有 HAL 层都依赖 Arduino API。

**Arduino API 依赖清单**:

| Arduino API | 使用位置 | ESP-IDF 替代 |
|------------|---------|-------------|
| `delay(ms)` | `main.cpp:138,264`, `hal_system.cpp:66` | `esp_timer` 或忙等待 |
| `millis()` | 多处时间相关代码 | `esp_timer_get_time() / 1000` |
| `Serial.begin/print/read` | `main.cpp`, `dev_ttyS0.cpp`, Shell | `driver/uart.h` (UART 驱动) |
| `M5.begin()` | `main.cpp:141` | 手动初始化 I2C、SPI、电源管理 |
| `M5.Display.*` | `main.cpp`, `hal_display.cpp` | 裸机 ST7789 SPI 驱动 |
| `M5.BtnA/B.read()` | `hal_input.cpp` | GPIO 中断 + 去抖状态机 |
| `M5.Axp.*` | 电源管理 | `driver/i2c.h` + AXP192 寄存器读写 |
| `ESP.getFreeHeap()` | `main.cpp` | `esp_get_free_heap_size()` |
| `WiFi.h` | `wifi_manager.cpp` | `esp_wifi.h` (ESP-IDF WiFi API) |
| `NimBLEDevice.h` | `bt_manager.cpp` | `nimble/blecent.h` (ESP-IDF NimBLE) |

**任务列表**:
- [ ] 3.1 修改 `platformio.ini`：`framework = espidf`，配置 ESP-IDF 版本和组件
- [ ] 3.2 创建 `src/main.c`：实现 `app_main()` 替代 Arduino `setup()`/`loop()`
- [ ] 3.3 重写 `hal_system.cpp`：`hal_delay_ms()` 基于 `esp_timer`，`hal_millis()` 基于 `esp_timer_get_time`
- [ ] 3.4 重写 `hal_display.cpp`：裸机 ST7789 SPI 驱动（初始化序列、`pushSprite` DMA）
- [ ] 3.5 重写 `hal_input.cpp`：GPIO 中断 + 去抖状态机替代 `M5.BtnA.wasPressed()`
- [ ] 3.6 重写 `dev_ttyS0.cpp`：ESP-IDF UART 驱动替代 `Serial`
- [ ] 3.7 迁移 `wifi_manager.cpp`：`WiFi.h` → `esp_wifi.h` API（状态机逻辑保持不变）
- [ ] 3.8 迁移 `bt_manager.cpp`：`NimBLEDevice.h` → ESP-IDF NimBLE API
- [ ] 3.9 编写 AX192 电源管理驱动（I2C 寄存器读写）替代 `M5.Axp`

**验证**:
```bash
pio run -e m5stick-c
# 烧录后：Boot logo → 菜单 → 按键正常 → WiFi 扫描/连接 → BT 扫描/配对
```

**回滚**: `git revert` 整个 commit。此步骤为**重大架构变更**，不可部分回退。

**预计改动文件**:
- `platformio.ini` — framework 切换
- `src/main.c` — **新建**（替代 `main.cpp`）
- `src/hal/hal_system.cpp` — 重写
- `src/hal/hal_display.cpp` — 重写（裸机 ST7789）
- `src/hal/hal_input.cpp` — 重写（GPIO 去抖）
- `src/kernel/devices/dev_ttyS0.cpp` — UART 驱动替代
- `src/app/wifi/wifi_manager.cpp` — ESP-IDF WiFi API
- `src/app/bluetooth/bt_manager.cpp` — ESP-IDF NimBLE API
- `src/hal/axp192.cpp/h` — **新建**（电源管理）
- `src/hal/hal_display.h` — 接口不变

---

## Step 4: 原生测试环境升级

**目标**: 原生测试环境也使用 XEROS_NATIVE_SCHED 路径（而非独立 FreeRTOS 桩）

**上下文摘要**:
当前 `NATIVE_TEST` 使用 POSIX `ucontext` 上下文切换。XEROS_NATIVE_SCHED 启用后，硬件平台使用 `setjmp/longjmp`。两者路径独立。Step 4 统一两者：原生测试也通过 XEROS_NATIVE_SCHED 路径编译（`kern_ctx_esp32.h` 在 `NATIVE_TEST` 下被 `#ifndef` 排除，需为 macOS/Linux 提供 `kern_ctx_native.h`）。

**任务列表**:
- [ ] 4.1 创建 `kern_ctx_native.h`：基于 POSIX `ucontext` 的上下文切换（与当前 `NATIVE_TEST` 路径一致）
- [ ] 4.2 修改条件编译：`NATIVE_TEST` 不再走独立路径，改为 `XEROS_NATIVE_SCHED` + `kern_ctx_native.h`
- [ ] 4.3 统一 `kern_task.c` 中的 `#elif defined(XEROS_NATIVE_SCHED)` 路径覆盖原生测试
- [ ] 4.4 添加回归测试：kill/protected/nonexist/virtual/idempotent

**验证**:
```bash
pio test -e native
# 所有测试通过，包括 KernTaskKillTest
```

**预计改动文件**:
- `src/kernel/kern_ctx_native.h` — **新建**
- `src/kernel/kern_task.c` — 统一条件编译
- `src/kernel/kern_task.h` — 统一上下文类型

---

## Step 5: 清理与文档

**目标**: 删除所有 FreeRTOS 残留代码，更新文档

**任务列表**:
- [ ] 5.1 删除 `kern_port.c` 中 `#else /* FreeRTOS 后端 */` 路径（保留桩）
- [ ] 5.2 删除 `kern_port_native.c` 中 `#ifndef XEROS_NATIVE_SCHED` 守卫（使其始终编译）
- [ ] 5.3 合并 `kern_port.c` 和 `kern_port_native.c`（两者功能不再重叠）
- [ ] 5.4 从 `platformio.ini` 库依赖中删除 `framework = arduino` 相关
- [ ] 5.5 更新 `doc/kernel/kern-port.md`：标注 FreeRTOS 已移除
- [ ] 5.6 更新 `CLAUDE.md`：移除 FreeRTOS 相关约定
- [ ] 5.7 全局搜索注释中的 "FreeRTOS" 并更新

**验证**:
```bash
grep -r "FreeRTOS\|freertos" src/ doc/ --include="*.c" --include="*.h" --include="*.cpp" --include="*.md"
# 预期输出：仅 doc/ 中历史文档提及（标记为 "已移除"）
```

---

## 依赖关系图

```
Step 1 (XEROS_NATIVE_SCHED 修复)
  ├── Step 2 (源码 FreeRTOS 清理) ← 可与 Step 1 并行
  │     └── Step 3 (Arduino→ESP-IDF) ← 依赖 Step 1 + Step 2
  │           └── Step 4 (测试统一) ← 可与 Step 3 部分并行
  │                 └── Step 5 (清理文档)
```

**并行机会**: Step 1 和 Step 2 可同时进行（修改不同文件，无冲突）

---

## 全局不变量

每个 Step 完成后必须验证：

1. **编译通过**: `pio run -e m5stick-c` 无错误
2. **原生测试通过**: `pio test -e native` 全部通过
3. **无新编译警告**: `-Wall -Wextra` 零新增警告
4. **代码风格**: 符合 `doc/coding-style.md` 约定
5. **Git 可回退**: 每个 Step 独立 commit

---

## 风险与缓解

| 风险 | 严重程度 | 缓解措施 |
|------|---------|---------|
| `kern_ctx_esp32.h` 汇编仍有 bug | 🔴 阻塞 | 在 ESP32 上单独调试验证，使用逻辑分析仪观察 WDT |
| ST7789 裸机驱动不稳定 | 🟡 中等 | 参考 M5GFX/LovyanGFX 源码，分步实现（先初始化 → 画点 → 帧缓冲 → DMA） |
| MPU6886 I2C 驱动时序问题 | 🟡 中等 | 使用 ESP-IDF I2C 驱动，匹配 datasheet 时序参数 |
| WiFi 从 Arduino 迁移到 ESP-IDF | 🟡 中等 | WiFi 状态机逻辑不变，仅替换底层 API 调用 |
| NimBLE 从 Arduino 迁移到 ESP-IDF | 🟡 中等 | ESP-IDF 自带 NimBLE 示例，API 差异较小 |
| 编译时间显著增加 | 🟢 低 | ESP-IDF 首次编译较慢，增量编译可接受 |

---

## 预计工作量

| Step | 预计时间 | 复杂度 |
|------|---------|--------|
| Step 1: 汇编修复 | 2-4 小时 | 🔴 高 |
| Step 2: 源码清理 | 1-2 小时 | 🟢 低 |
| Step 3: Arduino→ESP-IDF | 8-16 小时 | 🔴 极高 |
| Step 4: 测试统一 | 2-3 小时 | 🟡 中等 |
| Step 5: 清理文档 | 1 小时 | 🟢 低 |
| **总计** | **14-26 小时** | |

---

*计划由 blueprint 生成。修改计划前请参阅 plan-mutation-protocol。*
