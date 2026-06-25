/**
 * @file   main.cpp
 * @brief  M5Stick-C 固件主入口 (ESP-IDF)
 * @details 硬件环境主程序：初始化 ESP-IDF 子系统、NVS 存储、设置、显示驱动、
 *          UI 菜单及管理器，进入主循环处理输入、更新状态机及渲染 UI。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>

#include "app/storage/storage.h"
#include "app/settings/settings.h"
#include "app/app_init.h"
#include "app/app_state.h"
#include "ui/ui_widget.h"  /* xerintosh_push_pop_up */

extern "C" {
void wifi_mgr_task_main(void *arg);
}

#ifndef NATIVE_TEST

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_uart.h"
#include "kernel/debug_serial.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "app/serial_input/serial_input.h"
#include "app/serial_monitor/serial_monitor.h"
#include "app/wifi/wifi_manager.h"
#include "app/boot/boot_screen.h"

/* Xeros 内核 */
#include "kernel/kern_init.h"
#include "kernel/kern_task.h"
#include "kernel/kern_smp.h"
#include "kernel/kern_kmalloc.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_devfs.h"
#include "kernel/kern_procfs.h"
#include "kernel/kern_sysfs.h"
#include "kernel/kern_gpiofs.h"
#include "kernel/devices/kern_devices.h"
#include "kernel/devices/dev_ttyS0.h"
#include "kernel/kern_shell.h"
#include "app/app_mem.h"

/* UI 任务入口 */
extern "C" void ui_task_main(void *arg);

/* ═══ 设置变更回调（由 app_init.c 引用）═══ */

static int16_t brightness = 50;  /* 当前硬件亮度缓存 */

/**
 * @brief 亮度变更回调
 * @note  将亮度等级转换为硬件 PWM 值并应用到屏幕
 */
extern "C" void on_brightness_change_cb(void *ud)
{
    (void)ud;
    brightness = settings_brightness_hw_value();
    hal_display_set_brightness((uint8_t)brightness);
    storage_set_brightness(settings_get_brightness());
}

/**
 * @brief 动画速度变更回调
 */
extern "C" void on_anim_speed_change_cb(void *ud)
{
    (void)ud;
    g_anim_speed = settings_anim_speed_value();
    storage_set_anim_speed((uint8_t)g_anim_speed);
}

/**
 * @brief 动画开关变更回调
 */
extern "C" void on_anim_enabled_change_cb(void *ud)
{
    (void)ud;
    storage_set_anim_enabled(g_anim_enabled);
    xerintosh_push_pop_up(g_anim_enabled ? "动画已开启" : "动画已关闭", 1000);
}

/**
 * @brief 弹簧动画风格变更回调（动弹/普通）
 */
extern "C" void on_spring_mode_change_cb(void *ud)
{
    (void)ud;
    storage_set_spring_mode(g_spring_anim_mode);
    xerintosh_push_pop_up(g_spring_anim_mode ? "已切换为动弹" : "已切换为普通", 1000);
}

/**
 * @brief 弹簧硬度变更回调
 */
extern "C" void on_spring_stiffness_change_cb(void *ud)
{
    (void)ud;
    if (!g_spring_anim_mode) {
        g_spring_stiffness_level = storage_get_spring_stiffness();
        xerintosh_push_pop_up("请先切换为动弹模式", 1500);
        return;
    }
    settings_set_spring_stiffness(g_spring_stiffness_level);  /* 即时生效 */
    storage_set_spring_stiffness(g_spring_stiffness_level);
}

/**
 * @brief 弹簧阻尼变更回调
 */
extern "C" void on_spring_damping_change_cb(void *ud)
{
    (void)ud;
    if (!g_spring_anim_mode) {
        g_spring_damping_level = storage_get_spring_damping();
        xerintosh_push_pop_up("请先切换为动弹模式", 1500);
        return;
    }
    settings_set_spring_damping(g_spring_damping_level);  /* 即时生效 */
    storage_set_spring_damping(g_spring_damping_level);
}

/**
 * @brief 波特率变更回调
 * @note  保存新波特率等级到 NVS，并重新初始化 UART
 */
extern "C" void on_serial_baud_change_cb(void *ud)
{
    (void)ud;
    storage_set_serial_baud_rate(g_serial_baud_rate);
    hal_uart0_set_baudrate((uint32_t)settings_serial_baud_hw_value(g_serial_baud_rate));
}

/**
 * @brief 屏幕方向变更回调
 * @note  M5StickC 实测 rotation 效果：
 *        setRotation(0) -> 正常竖屏 (portrait)
 *        setRotation(1) -> 正常横屏 (landscape)
 *        setRotation(2) -> 反向竖屏
 *        setRotation(3) -> 反向横屏
 */
extern "C" void on_screen_rotation_change_cb(void *ud)
{
    (void)ud;
    /* 同步 bool 开关到等级值 */
    g_screen_rotation_level = g_is_landscape ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT;

    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    hal_display_set_rotation(gfx_rotation);
    storage_set_screen_rotation((uint8_t)g_screen_rotation_level);
    hal_display_init();

    /* 强制重置退场动画状态机，避免 static 变量 _temp_h/_temp_h_trg
     * 残留旧屏幕尺寸的目标值，导致后续 user_item 进场动画卡死 */
    g_xerintosh_exit_animation_finished = true;
    g_xerintosh_exit_animation_status = 0;
}

/* ═══ 入口 ═══ */

static void deferred_kernel_init(void);

static void main_loop_task(void *arg)
{
    (void)arg;
    for (;;) {
        dev_ttyS0_poll();
        serial_monitor_update();
        wifi_mgr_process_requests();

        /* Periodic stack high-water profiling (once per second) */
        static uint32_t s_profile_last_ms = 0;
        uint32_t profile_now = hal_get_ticks();
        if (profile_now - s_profile_last_ms >= 1000) {
            s_profile_last_ms = profile_now;
            kern_task_t *cur = kern_task_current();
            if (cur != NULL) {
                kern_task_stack_profile_record(cur->name,
                                               kern_task_stack_highwater(cur));
            }
        }

        /* Yield / sleep to let other Xeros tasks run */
        kern_sleep_ms(1);
    }
}

/**
 * @brief ESP-IDF app_main()：系统初始化
 * @note  初始化顺序：UART -> 存储 -> 设置 -> 显示 -> UI -> 管理器
 */
extern "C" void app_main(void)
{
    /* 最早进行 UART 初始化，确保后续 printf/日志能立即输出 */
    hal_uart0_init();
    hal_delay_ms(100);
    debug_printf("\n[  BOOT] M5Stick-P1 kernel starting...\n");

    debug_printf("[  OK  ] UART initialized\n");

    /* 初始化 ESP-IDF NVS（替代 Arduino Preferences 底层） */
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_rc = nvs_flash_init();
    }
    if (nvs_rc != ESP_OK) {
        debug_printf("[ FAIL ] NVS flash init: %d\n", nvs_rc);
    }

    storage_init();
    debug_printf("[  OK  ] NVS storage\n");
    settings_load_from_storage();
    debug_printf("[  OK  ] Settings loaded from NVS\n");

    /* M5StickC 实测 rotation 效果：
     *   setRotation(0) -> 正常竖屏 (portrait)
     *   setRotation(1) -> 正常横屏 (landscape)
     * 默认使用与 Arduino 版本一致的横屏方向。 */
    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    hal_display_set_rotation(gfx_rotation);

    debug_printf("[  OK  ] Display driver, free_heap=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    hal_display_init();

    brightness = settings_brightness_hw_value();
    hal_display_set_brightness((uint8_t)brightness);
    g_anim_speed = settings_anim_speed_value();

    hal_system_init();
    hal_input_init();

    boot_screen_show();
    debug_printf("[  OK  ] Boot screen\n");

    app_init_ui();
    debug_printf("[  OK  ] UI initialised\n");

    debug_printf("[  OK  ] App managers, free_heap=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    app_init_managers();

    xerintosh_init_core();
    debug_printf("[  OK  ] Xeros core\n");
    g_in_xerintosh = true;

    /* 内核初始化延迟到 loop() 第一帧，避免 setup() 累积时间
     * 触发 TG1 系统看门狗（FreeRTOS idle 任务在 setup 返回后喂狗） */
    debug_printf("[  OK  ] Hardware init complete, deferring kernel init\n");

    /* 主循环 */
    for (;;) {
        deferred_kernel_init();

        /* 消费硬件定时器 ISR 设置的抢占 tick 请求，并在当前 CPU 触发重调度。
         * 必须在 kern_sched_tick() 之前消费，确保 scheduler 在挑选下一个任务
         * 前能看到最新的抢占状态，减少高优先级任务就绪后的调度延迟。 */
        if (kern_port_preempt_consume()) {
            g_need_resched = true;
        }

        kern_sched_tick();

        /*
         * 显式让出 CPU 给 FreeRTOS idle 任务，确保中断看门狗（INT_WDT, 300ms）
         * 能被喂食。Without this, the Xeros scheduler task (priority 1) would
         * starve the FreeRTOS idle task (priority 0) on Core 0, causing INT_WDT
         * to fire and reset the system.
         */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ═══ 延迟内核初始化（第一帧）═══ */

static bool g_kernel_inited = false;

/* 任务栈大小（字节），基于阶段 2.1 后实际负载估算 */
#define UI_TASK_STACK_SIZE      4096
#define WIFI_MGR_STACK_SIZE     4096

static void deferred_kernel_init(void)
{
    if (g_kernel_inited) return;
    g_kernel_inited = true;

    /* -- 内核子系统初始化 -- */
    kern_init();
    kern_log_set_level(KERN_LOG_INFO);
    kern_vfs_init();
    kern_devfs_init();
    kern_procfs_init();
    kern_sysfs_init();

    /* -- 设置系统保留内存 --
     * 保留水位是系统应急缓冲，不应等于所有服务同时开启所需内存之和。
     * 当前 DRAM 约 200KB，UI/显示层已占约 140KB，空闲约 60KB，因此
     * 保留水位必须保持较小（8KB），否则 WiFi/BT 的正常启用都会被拒绝。 */
    kern_kmem_set_reserved_bytes(8 * 1024);

    /* -- GPIO 文件系统 -- */
    kern_err_t gpio_rc = kern_gpiofs_init();
    if (gpio_rc != KERN_OK) {
        kern_log(KERN_LOG_ERROR, "gpiofs init failed: %d", gpio_rc);
    }

    /* -- sysfs -> 硬件双向绑定 -- */

    /* brightness: sysfs 写入时同步到屏幕背光 */
    kern_sysfs_bind(KERN_SYSFS_BRIGHTNESS,
        [](kern_sysfs_attr_t attr, int32_t val, void *ud) {
            (void)attr; (void)ud;
            uint8_t hw = (uint8_t)(val > 255 ? 255 : val);
            hal_display_set_brightness(hw);
            brightness = (int16_t)val;
            /* sysfs brightness 是 0-255 HW 值，storage 期望 1-10 level */
            int16_t level = settings_brightness_level_from_hw((int16_t)val);
            storage_set_brightness((uint8_t)level);
        }, NULL);

    /* rotation: sysfs 写入时同步到屏幕方向 */
    kern_sysfs_bind(KERN_SYSFS_ROTATION,
        [](kern_sysfs_attr_t attr, int32_t val, void *ud) {
            (void)attr; (void)ud;
            if (val < 0 || val > 3) return;
            hal_display_set_rotation((int)val);
            g_screen_rotation_level = (val == 0 || val == 2)
                                      ? ORIENTATION_PORTRAIT
                                      : ORIENTATION_LANDSCAPE;
            g_is_landscape = (g_screen_rotation_level == ORIENTATION_LANDSCAPE);
            storage_set_screen_rotation((uint8_t)g_screen_rotation_level);
            hal_display_init();
            g_xerintosh_exit_animation_finished = true;
            g_xerintosh_exit_animation_status = 0;
        }, NULL);

    /* anim_speed: sysfs 写入时同步到全局动画速度 */
    kern_sysfs_bind(KERN_SYSFS_ANIM_SPEED,
        [](kern_sysfs_attr_t attr, int32_t val, void *ud) {
            (void)attr; (void)ud;
            if (val < 0 || val > 100) return;
            g_anim_speed = (int16_t)val;
            storage_set_anim_speed((uint8_t)val);
        }, NULL);

    /* anim_enabled: sysfs 写入时同步到全局开关 */
    kern_sysfs_bind(KERN_SYSFS_ANIM_ENABLED,
        [](kern_sysfs_attr_t attr, int32_t val, void *ud) {
            (void)attr; (void)ud;
            g_anim_enabled = (val != 0);
            storage_set_anim_enabled(g_anim_enabled);
        }, NULL);

    /* 同步 sysfs 初始值为当前硬件真实状态（K13）
     * 避免用户 cat 到默认值后写入相同值却无变化。 */
    kern_sysfs_update(KERN_SYSFS_BRIGHTNESS,   (int32_t)brightness);
    kern_sysfs_update(KERN_SYSFS_ROTATION,     (int32_t)(g_is_landscape ? 1 : 0));
    kern_sysfs_update(KERN_SYSFS_ANIM_SPEED,   (int32_t)g_anim_speed);
    kern_sysfs_update(KERN_SYSFS_ANIM_ENABLED, g_anim_enabled ? 1 : 0);

    kern_devices_init();
    debug_printf("[  OK  ] Kernel subsystems, free_heap=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    /* -- 启动 Shell -- */
    kern_shell_init();

    /* -- 启动 UI/WiFi/BT 任务 --
     * 使用历史栈高水位画像推荐栈大小，首次启动无画像时回退到经验值。 */
    size_t ui_stack   = kern_task_stack_recommend_by_name("ui",       UI_TASK_STACK_SIZE);
    size_t wifi_stack = kern_task_stack_recommend_by_name("wifi-mgr", WIFI_MGR_STACK_SIZE);

    kern_pid_t ui_pid = kern_spawn("ui", ui_task_main, NULL, ui_stack);
    debug_printf("[  OK  ] UI task spawned (pid=%d, stack=%u)\n", ui_pid, (unsigned)ui_stack);

    /* WiFi 管理器作为独立内核任务运行，与 UI 任务解耦 */
    kern_spawn("wifi-mgr", wifi_mgr_task_main, NULL, wifi_stack);
    debug_printf("[  OK  ] WiFi manager spawned as kernel task (stack=%u)\n", (unsigned)wifi_stack);

    /* 主循环任务：串口轮询、串口监视器、WiFi 请求处理 */
    kern_spawn("main-loop", main_loop_task, NULL, 4096);
    debug_printf("[  OK  ] Main loop task spawned as kernel task (stack=4096)\n");

    /* 让出 CPU 给 FreeRTOS，使刚创建的任务有机会启动并阻塞在调度信号量上 */
    hal_delay_ms(10);

    kern_log(KERN_LOG_INFO, "Xeros kernel boot complete, entering scheduler");
    debug_printf("[  OK  ] Kernel boot complete, entering scheduler loop\n");
}

#endif /* NATIVE_TEST */
