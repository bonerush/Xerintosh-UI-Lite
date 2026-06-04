/**
 * @file   ui_task.c
 * @brief  用户态 UI 任务入口（Xerintosh 内核任务包装）
 * @details 将现有 Xerintosh UI 主循环包装为内核任务 `ui_task_main`。
 *          每帧：输入 → 清屏 → UI 渲染 → 长按提示 → 刷新 → yield。
 *
 *          当前阶段 HAL 调用保持直接（不经过 VFS），后续逐步迁移到
 *          /dev/fb0 write + /dev/input0 read。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_drawer.h"
#include "kernel/kern_task.h"
#include "kernel/kern_init.h"

/* ─── 外部函数 ─── */
extern void app_input_process(void);
/* WiFi/BT 管理器已作为独立内核任务运行（wifi_mgr_task_main / bt_mgr_task_main），
   不再在 UI 任务中调用 _update() */

/* ═══ UI 任务入口 ═══ */

/**
 * @brief UI 任务入口函数
 * @note  由 kern_spawn("ui", ui_task_main, NULL, 4096) 启动
 */
void ui_task_main(void *arg)
{
    (void)arg;
    static int frame = 0;

    kern_log(KERN_LOG_INFO, "ui_task_main started");

    for (;;) {
        frame++;
        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d begin", frame);
        }

        app_input_process();

        hal_display_clear();
        xerintosh_ui_main_core();
        xerintosh_ui_widget_core();

        /* 长按提示动画 */
        uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
        uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
        if (dur_a > 0 && dur_a < 500) {
            xerintosh_draw_long_press_hint(dur_a, 500);
        } else if (dur_b > 0 && dur_b < 500) {
            xerintosh_draw_long_press_hint(dur_b, 500);
        }

        hal_display_flush();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d flush done, yielding", frame);
        }

#ifndef NATIVE_TEST
        /* 释放 1ms 给 FreeRTOS idle 任务（优先级 0），
         * 确保 TG1 系统看门狗能被及时喂狗。
         * 内核任务和 Arduino loop 都在优先级 1，
         * 若无此让步则 idle 任务会被永久饿死。 */
        delay(1);
#endif

        /* 让出 CPU */
        kern_yield();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d resumed after yield", frame);
        }
    }
}
