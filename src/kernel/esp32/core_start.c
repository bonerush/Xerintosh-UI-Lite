#include "core_start.h"

#if defined(XEROS_NATIVE_SCHED) && defined(CONFIG_SMP_ENABLED) && !defined(NATIVE_TEST)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "kernel/kern_init.h"

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

    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "SMP: native core start failed for %d", cpu_id);
    }
}

#else

void xeros_core_start(uint8_t cpu_id, void (*entry)(void *arg))
{
    (void)cpu_id;
    (void)entry;
}

#endif
