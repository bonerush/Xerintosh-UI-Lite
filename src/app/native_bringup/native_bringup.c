/**
 * @file   native_bringup.c
 * @brief  Xeros 原生调度器 Phase 1 最小测试入口
 * @details 创建一个 blink 任务，每 500ms 在串口输出计数，
 *          用于验证原生上下文切换与调度器在 ESP32 硬件上稳定运行。
 *
 * @copyright Copyright (c) 2026
 */

#include "native_bringup.h"
#include "kernel/kern_task.h"
#include "kernel/debug_serial.h"

/* M5Stick-C 板载 LED 通常接 GPIO10；Phase 1 先以串口输出为主，
 * 避免 GPIO 配置引入额外崩溃点。 */

static void blink_task(void *arg)
{
    (void)arg;
    int count = 0;

    debug_printf("[native] blink_task started\n");
    for (;;) {
        debug_printf("[native] blink count=%d\n", count);
        count++;
        kern_sleep_ms(500);
    }
}

void native_bringup_init(void)
{
    kern_pid_t blink_pid = kern_spawn("blink", blink_task, NULL, 2048);
    if (blink_pid < 0) {
        debug_printf("[native] failed to spawn blink task: %d\n", blink_pid);
    } else {
        debug_printf("[native] blink task spawned (pid=%d)\n", blink_pid);
    }
}
