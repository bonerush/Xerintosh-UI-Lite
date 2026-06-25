# 当前剩余 FreeRTOS 引用与借鉴清单

> **Parent:** [项目索引](index.md)  
> **Related:** [Xeros 内核重构计划](../.claude/plans/tingly-mixing-sundae.md)

## 概述

本清单记录移除 `freertos_compat.h` 兼容层后，代码库中**仍然显式引用或借鉴 FreeRTOS API/概念**的位置。目的是为后续进一步自主化改造提供清晰的路线图。

清单按性质分为五类：

1. [有意保留的 fallback 后端](#1-有意保留的-fallback-后端)
2. [ESP-IDF 驱动内部依赖](#2-esp-idf-驱动内部依赖)
3. [注释与文档中的引用](#3-注释与文档中的引用)
4. [本次改造后仍显式使用的 FreeRTOS API](#4-本次改造后仍显式使用的-freertos-api)
5. [建议后续自主化改造项](#5-建议后续自主化改造项)

---

## 1. 有意保留的 fallback 后端

这些文件在 `XEROS_NATIVE_SCHED` 未定义时提供 FreeRTOS 后端实现，属于刻意保留的双后端设计。

### `src/kernel/kern_port_freertos.c`

*📄 Source: [kern_port_freertos.c](../src/kernel/kern_port_freertos.c#L212-L537)*

- 完整的 FreeRTOS 任务容器后端。
- 使用 `xTaskCreatePinnedToCore`、`vTaskDelay`、`vTaskDelete`。
- 使用 `xSemaphoreCreateBinary`、`xSemaphoreGive`、`xSemaphoreTake` 实现跨 CPU 调度令牌。
- **改造难度**：高（需彻底替换为 Xeros 原生 SMP 实现）。
- **建议后续动作**：当 Xeros 原生 SMP 成熟后，删除本文件或将其缩减为 ESP-IDF 任务启动桩。

### `src/kernel/kern_smp.c`

*📄 Source: [kern_smp.c](../src/kernel/kern_smp.c#L23-L79)*

- `XEROS_NATIVE_SCHED` 路径下使用 `xTaskCreatePinnedToCore` 创建 per-CPU Xeros 调度器任务。
- **改造难度**：高（涉及双核调度器任务绑定）。
- **建议后续动作**：研究 ESP-IDF 的 `esp_ipc` 或自定义 per-CPU 入口，逐步替代 `xTaskCreatePinnedToCore`。

### `src/kernel/kern_task_stack.c`

*📄 Source: [kern_task_stack.c](../src/kernel/kern_task_stack.c#L145-L147)*

- `#else` 分支使用 `uxTaskGetStackHighWaterMark` 查询 FreeRTOS 任务栈高水位。
- **改造难度**：中（原生调度器路径已用 Xeros 自己的栈画像替代）。
- **建议后续动作**：保留 fallback；当只保留原生调度器时可删除该分支。

### `src/kernel/kern_sched.c`

*📄 Source: [kern_sched.c](../src/kernel/kern_sched.c#L444-L445)*

- `#else` 分支包含 `<freertos/FreeRTOS.h>` 和 `<freertos/task.h>`，用于 FreeRTOS 后端的调度 tick 消费。
- **改造难度**：中。
- **建议后续动作**：随 `kern_port_freertos.c` 一起移除。

---

## 2. ESP-IDF 驱动内部依赖

以下组件并非由本项目直接调用 FreeRTOS API，而是 ESP-IDF 及其第三方库内部使用。只要继续使用这些库，就无法完全消除 FreeRTOS。

| 组件 | 说明 |
|------|------|
| **ESP-IDF WiFi/BT/TCP/IP** | `esp_wifi`、`esp_netif`、`esp_event` 等内部任务基于 FreeRTOS。 |
| **LovyanGFX** | 显示库内部可能使用 FreeRTOS 任务/队列。 |
| **cJSON** | 纯 C 库，无 FreeRTOS 依赖。 |
| **ESP-IDF 系统服务** | NVS、SPI Flash、UART 驱动等运行在 ESP-IDF 任务上下文。 |

**改造难度**：极高（需替换或重写驱动层）。  
**建议后续动作**：在可预见的未来接受这些底层依赖；若追求完全自主化，需评估自研驱动或替换为更轻量的 RTOS 抽象层。

---

## 3. 注释与文档中的引用

以下位置仅在注释、文档字符串中提到 FreeRTOS，代码本身已不再依赖。

- `src/kernel/kern_port.h`：注释说明 FreeRTOS 后端历史设计。
- `src/kernel/kern_port_native.c`：注释提到 `vTaskDelay` 替代方案。
- `src/kernel/kern_task.h`：注释提到 `uxTaskGetStackHighWaterMark`。
- `src/hal/hal_system.cpp`：文件头注释提到 ESP-IDF 使用 `esp_timer_get_time() / vTaskDelay()`。
- `src/app/ui_task.c`：注释提到替代 `vTaskDelay`。

**改造难度**：低。  
**建议后续动作**：逐步更新注释，使其反映当前 Xeros 原生实现，避免误导后续维护者。

---

## 4. 本次改造后仍显式使用的 FreeRTOS API

这些是本项目中**仍然直接调用**的 FreeRTOS API，且经过有意识的设计决策保留。

### `src/main.cpp` — `vTaskDelay(pdMS_TO_TICKS(1))`

*📄 Source: [main.cpp](../src/main.cpp#L285)*

- 位于 `app_main()` 的调度器宿主循环中。
- 作用：喂 ESP-IDF 中断看门狗（INT_WDT），并让出 CPU 给 ESP-IDF 内部任务（WiFi/事件等）。
- **保留原因**：`app_main()` 运行在 FreeRTOS 任务上下文，必须显式让出 CPU；`kern_sleep_ms` 在此上下文无效。
- **改造难度**：中。
- **建议后续动作**：可改为 `esp_task_wdt_reset()` + 强制切换到低优先级任务，但需验证 WiFi 稳定性。

### `src/hal/hal_system.cpp` — `vTaskDelay(pdMS_TO_TICKS(ms))`

*📄 Source: [hal_system.cpp](../src/hal/hal_system.cpp#L68-L74)*

- 位于 `hal_delay_ms()` 的非 Xeros 上下文分支。
- 作用：启动阶段或 FreeRTOS 回调中延时。
- **保留原因**：这些上下文不在 Xeros 任务中，不能使用 `kern_sleep_ms`。
- **改造难度**：低（当前已是上下文感知实现）。
- **建议后续动作**：若后续所有调用点都进入 Xeros 任务上下文，可删除该分支。

---

## 5. 建议后续自主化改造项

| 优先级 | 目标 | 文件/位置 | 改造思路 |
|--------|------|-----------|----------|
| 高 | 完全消除 `app_main()` 中的 `vTaskDelay(1)` | `src/main.cpp:285` | 用 `esp_task_wdt_reset()` + 低优先级任务切换替代，或重构为纯中断驱动调度器宿主。 |
| 高 | 原生 SMP 调度器任务创建 | `src/kernel/kern_smp.c:79` | 替代 `xTaskCreatePinnedToCore`，研究 ESP-IDF IPC 或自定义入口。 |
| 中 | 清理 fallback 后端 | `src/kernel/kern_port_freertos.c` | 当原生调度器成为唯一后端时删除。 |
| 中 | 更新过时注释 | 多处 `.c/.h` | 将 FreeRTOS 相关注释改为 Xeros 原生描述。 |
| 低 | `hal_delay_ms()` 完全原生化 | `src/hal/hal_system.cpp` | 确认所有调用点均在 Xeros 任务上下文后删除 FreeRTOS 分支。 |

---

## 验证命令

以下命令用于定期检查 FreeRTOS 引用残留：

```bash
grep -rn "vTaskDelay\|xTaskCreate\|xSemaphore\|portMUX\|portENTER_CRITICAL\|portEXIT_CRITICAL\|uxTaskGetStackHighWaterMark" \
  src/ --include="*.{c,cpp,h}"
```

当前仅应在以下位置命中：

- `src/kernel/kern_port_freertos.c`（fallback 后端）
- `src/kernel/kern_smp.c`（原生 SMP 任务创建）
- `src/kernel/kern_task_stack.c`（fallback 栈查询）
- `src/kernel/kern_sched.c`（fallback 调度 tick）
- `src/main.cpp`（调度器宿主喂狗）
- `src/hal/hal_system.cpp`（启动阶段延时 fallback）
- 注释与文档字符串

---

> **注意**：本清单为动态文档。每当移除一处 FreeRTOS 引用或新增 fallback 时，应同步更新本文档。
