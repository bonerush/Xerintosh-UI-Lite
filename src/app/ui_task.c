/**
 * @file   ui_task.c
 * @brief  用户态 UI 任务入口（Xerintosh 内核任务包装）
 * @details 将现有 Xerintosh UI 主循环包装为内核任务 `ui_task_main`。
 *          每帧：输入 → 清屏 → UI 渲染 → 长按提示 → 刷新 → yield。
 *
 *          VRR 可变刷新率：60Hz-100Hz 自适应。
 *          动画活跃时目标 100Hz (10ms)，静态画面降至 60Hz (16ms)。
 *          使用 hal_get_ticks() 进行帧时间追踪。
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
#include "ui/ui_dirty.h"
#include "ui/ui_drawer.h"
#include "kernel/kern_task.h"
#include "kernel/kern_init.h"
#include "kernel/kern_sync.h"

/* ─── 外部函数 ─── */
extern void app_input_process(void);

/* ═══ VRR 帧率控制 ═══ */

/* VRR 目标帧间隔（毫秒）：
 * - 动画活跃时 (HI): 10ms = 100Hz，提供丝滑视觉
 * - 静态画面时 (LO): 16ms ≈ 60Hz，省电并降低 SPI 总线压力
 * - 长按提示时 (HINT): 8ms ≈ 125Hz，确保进度环流畅 */
#define VRR_ANIM_ACTIVE_MS   10
#define VRR_IDLE_MS          16
#define VRR_HINT_MS          8

/* 帧计时器 */
static uint32_t s_frame_start_ms = 0;

/**
 * @brief 计算当前帧的目标间隔
 * @return 目标帧间隔（毫秒）
 */
static uint32_t vrr_target_interval(void)
{
    /* 长按提示最高优先级（需要丝滑的进度环） */
    uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
    uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
    if ((dur_a > 0 && dur_a < 500) || (dur_b > 0 && dur_b < 500)) {
        return VRR_HINT_MS;
    }

    /* 动画活跃时高频刷新 */
    if (xerintosh_is_dirty()) {
        return VRR_ANIM_ACTIVE_MS;
    }

    /* 静态画面低频刷新 */
    return VRR_IDLE_MS;
}

/**
 * @brief VRR 帧率限速：等待直到达到目标帧间隔
 * @note  使用 hal_get_ticks() 进行毫秒级计时
 */
static void vrr_wait_for_next_frame(void)
{
    uint32_t elapsed = hal_get_ticks() - s_frame_start_ms;
    uint32_t target = vrr_target_interval();

    if (elapsed < target) {
        uint32_t remain = target - elapsed;
        /* 剩余时间 > 1ms 时走内核 sleep 释放 CPU，
         * <= 1ms 时忙等以确保精确帧时序。 */
        if (remain > 1) {
            kern_sleep_ms(remain);
        }
        /* 无论是否 sleep，最后都 yield 让其他任务有机会运行 */
    }
}

/* ═══ UI 任务入口 ═══ */

/**
 * @brief UI 任务入口函数
 * @note  由 kern_spawn("ui", ui_task_main, NULL, 4096) 启动
 */
void ui_task_main(void *arg)
{
    (void)arg;
    static int frame = 0;

    kern_log(KERN_LOG_INFO, "ui_task_main started (VRR 60-100Hz)");
    s_frame_start_ms = hal_get_ticks();

    for (;;) {
        frame++;
        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d begin", frame);
        }

        app_input_process();

        /* 屏幕刷新策略：
         * - user_item 内部：始终清屏，因为框架无法预知 App 绘制内容
         * - 菜单列表层：脏矩形优化，仅 dirty 时清屏 */
        bool should_clear = xerintosh_is_in_user_item() || xerintosh_is_dirty();
        if (should_clear) {
            hal_display_clear();
        }
        xerintosh_ui_main_core();
        xerintosh_ui_widget_core();

        /* 长按提示动画（仅在需要时才 flush，避免静态帧无意义 SPI 推送） */
        uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
        uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
        bool has_hint = false;
        if (dur_a > 0 && dur_a < 500) {
            xerintosh_draw_long_press_hint(dur_a, 500);
            has_hint = true;
        } else if (dur_b > 0 && dur_b < 500) {
            xerintosh_draw_long_press_hint(dur_b, 500);
            has_hint = true;
        }

        /* 脏区域局部刷新策略：
         * - dirty 区域 < 全屏 50%：仅推送脏矩形区域（节省 SPI 带宽）
         * - dirty 区域 >= 50% 或全屏清屏后：全屏推送
         * - 无脏标志且无长按提示：跳过刷新 */
        if (should_clear || has_hint || xerintosh_is_dirty()) {
            const xerintosh_dirty_region_t *dr = xerintosh_get_dirty_region();
            int32_t dr_area = (int32_t)dr->w * (int32_t)dr->h;
            int32_t full_area = (int32_t)HAL_SCREEN_WIDTH * (int32_t)HAL_SCREEN_HEIGHT;

            if (should_clear || dr_area >= full_area / 2) {
                hal_display_flush();
            } else if (dr->active && dr_area > 0) {
                hal_display_flush_region(dr->x, dr->y, dr->w, dr->h);
            } else {
                hal_display_flush();  /* 回退：全屏推送 */
            }
        }

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d flush done, yielding", frame);
        }

#ifndef NATIVE_TEST
        /* VRR 帧率限速：达到目标帧间隔后再继续。
         * kern_sleep_ms() 内部已完成上下文切换，无需额外 kern_yield()。 */
        vrr_wait_for_next_frame();
#else
        /* Native 测试环境：没有真正的时间流逝，直接 yield */
        kern_yield();
#endif

        /* 记录本帧结束时间，作为下一帧的基准 */
        s_frame_start_ms = hal_get_ticks();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d resumed after yield", frame);
        }
    }
}
