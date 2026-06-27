# FreeRTOS 调度依赖移除记录

> **Parent:** [Xeros 内核文档](index.md)

## 保留的 FreeRTOS 使用

- ESP-IDF 驱动内部（WiFi/BT/TCP/IP/LovyanGFX）仍依赖 FreeRTOS 任务/队列，这是 ESP-IDF 架构决定。
- `xeros_core_start` 仍调用 `xTaskCreatePinnedToCore` 创建核心启动桩，但桩任务立即进入 Xeros 调度循环，不再使用 FreeRTOS 调度语义。

*📄 Source: [core_start.c](../../src/kernel/esp32/core_start.c#L9-L24)*

```c
void xeros_core_start(uint8_t cpu_id, void (*entry)(void *arg))
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        (TaskFunction_t)entry,
        "xeros_core",
        4096,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL,
        cpu_id
    );
    ...
}
```

## 已移除的显式依赖

- `vTaskDelay` → `kern_sleep_ms` / `ets_delay_us`

*📄 Source: [hal_system.cpp](../../src/hal/hal_system.cpp#L72-L78)*

```cpp
void hal_delay_ms(uint32_t ms) {
    if (g_current_task != NULL) {
        kern_sleep_ms(ms);
    } else {
        ets_delay_us(ms * 1000);
    }
}
```

- `xTaskCreatePinnedToCore` in scheduler → `xeros_core_start` stub

*📄 Source: [kern_smp.c](../../src/kernel/kern_smp.c)*

```c
kern_smp_start_core(...) {
    xeros_core_start(cpu_id, entry);
}
```

## 验证命令

```bash
pio run -e m5stick-c-native
pio run -e m5stick-c
pio test -e native
```

## 未解决问题

- 完全绕过 ESP-IDF 的核心启动需要直接操作复位寄存器，风险高，V2 保留最小启动桩。

---

> **See Also:** [原生内核架构](../architecture/xeros-native-kernel.md)
