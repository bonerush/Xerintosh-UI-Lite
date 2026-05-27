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
extern void wifi_mgr_update(void);
extern void bt_mgr_update(void);
/* WiFi/BT 任务函数已打包为 wifi_mgr_task_main/bt_mgr_task_main，
   待 UI/网络操作彻底解耦后可切换到独立内核任务 */

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
        wifi_mgr_update();
        bt_mgr_update();

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

        /* 协作式让出 CPU */
        kern_yield();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d resumed after yield", frame);
        }
    }
}
