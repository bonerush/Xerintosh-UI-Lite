/**
 * @file   ui_task.c
 * @brief  UI 任务入口（FreeRTOS 任务包装）
 * @details 将 Xerintosh UI 主循环包装为 FreeRTOS 任务 `ui_task_main`。
 *          每帧：输入 → 清屏 → UI 渲染 → 长按提示 → 刷新。
 *
 *          HAL 调用保持直接（不经过虚拟文件系统）。
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

/* ─── 外部函数 ─── */
extern void app_input_process(void);
/* WiFi/BT 管理器已作为独立内核任务运行（wifi_mgr_task_main / bt_mgr_task_main），
   不再在 UI 任务中调用 _update() */

/* ═══ UI 任务入口 ═══ */

/**
 * @brief UI 任务入口函数
 * @note  由 xTaskCreatePinnedToCore("ui", ...) 启动
 */
void ui_task_main(void *arg)
{
    (void)arg;

    for (;;) {
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

#ifndef NATIVE_TEST
        /* 释放 1ms 给 FreeRTOS idle 任务（优先级 0），
         * 确保 TG1 系统看门狗能被及时喂狗。
         * FreeRTOS 抢占式调度器会自动在各任务间切换，
         * 无需手动 kern_yield()。 */
        delay(1);
#endif
    }
}
