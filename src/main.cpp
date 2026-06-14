/**
 * @file   main.cpp
 * @brief  M5Stick-C 固件主入口
 * @details 硬件环境主程序：初始化 M5Unified、NVS 存储、设置、显示驱动、
 *          UI 菜单及管理器，进入主循环处理输入、更新状态机及渲染 UI。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>

#include "app/storage/storage.h"
#include "app/settings/settings.h"
#include "app/app_init.h"
#include "app/app_state.h"

extern "C" {
void wifi_mgr_task_main(void *arg);
void bt_mgr_task_main(void *arg);
}

#ifndef NATIVE_TEST

#include <M5Unified.h>
#include <M5GFX.h>

#include "hal/hal_system.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "app/serial_input/serial_input.h"
#include "app/serial_monitor/serial_monitor.h"
#include "app/wifi/wifi_manager.h"
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"
#include "app/boot/boot_screen.h"

/* Xeros 内核 */
#include "kernel/kern_init.h"
#include "kernel/kern_task.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_devfs.h"
#include "kernel/kern_procfs.h"
#include "kernel/kern_sysfs.h"
#include "kernel/kern_gpiofs.h"
#include "kernel/devices/kern_devices.h"
#include "kernel/devices/dev_ttyS0.h"
#include "kernel/kern_shell.h"

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
    brightness = g_brightness_level * 10;
    uint8_t hw = (uint8_t)settings_brightness_hw_value();
    hal_display_set_brightness(hw);
    storage_set_brightness(brightness);
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
}

/**
 * @brief 波特率变更回调
 * @note  保存新波特率等级到 NVS，并重新初始化 Serial
 */
extern "C" void on_serial_baud_change_cb(void *ud)
{
    (void)ud;
    storage_set_serial_baud_rate(g_serial_baud_rate);
    Serial.end();
    Serial.begin(settings_serial_baud_hw_value(g_serial_baud_rate));
}

/**
 * @brief 屏幕方向变更回调
 * @note  M5StickC 实测 rotation 效果：
 *        setRotation(0) → 正常竖屏 (portrait)
 *        setRotation(1) → 正常横屏 (landscape)
 *        setRotation(2) → 反向竖屏
 *        setRotation(3) → 反向横屏
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

/**
 * @brief Arduino setup()：系统初始化
 * @note  初始化顺序：M5 硬件 → 串口 → 存储 → 设置 → 显示 → UI → 管理器
 */
void setup()
{
    /* 最早进行串口初始化，确保后续 printf/日志能立即输出 */
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[  BOOT] M5Stick-P1 kernel starting...");

    M5.begin();
    Serial.println("[  OK  ] M5.begin()");

    storage_init();
    Serial.println("[  OK  ] NVS storage");
    settings_load_from_storage();
    Serial.println("[  OK  ] Settings loaded from NVS");

    brightness = g_brightness_level * 10;
    hal_display_set_brightness((uint8_t)settings_brightness_hw_value());
    g_anim_speed = settings_anim_speed_value();

    /* M5StickC 实测 rotation 效果：
     *   setRotation(0) → 正常竖屏 (portrait)
     *   setRotation(1) → 正常横屏 (landscape) */
    g_is_landscape = (g_screen_rotation_level == ORIENTATION_LANDSCAPE);
    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    hal_display_set_rotation(gfx_rotation);

    Serial.printf("[  OK  ] Display driver, free_heap=%u\n", ESP.getFreeHeap());
    hal_display_init();
    hal_system_init();
    hal_input_init();

    boot_screen_show();
    Serial.println("[  OK  ] Boot screen");

    app_init_ui();
    Serial.println("[  OK  ] UI initialised");

    Serial.printf("[  OK  ] App managers, free_heap=%u\n", ESP.getFreeHeap());

    /* 注意：不释放 BLE controller 内存。
     * esp_bt_controller_mem_release(ESP_BT_MODE_BLE) 与 BluetoothSerial
     * 不兼容——BluetoothSerial::begin() 使用 BTDM（双模）配置初始化
     * Bluedroid，仍会尝试初始化 BLE 组件。释放 BLE 内存后，初始化
     * 失败 cleanup 路径会触发 memset(NULL) StoreProhibited 崩溃。
     * BT 先于 WiFi 初始化且当前 free heap 充足（~91KB），保留 BLE
     * 内存不会导致 OOM。 */

    app_init_managers();

    xerintosh_init_core();
    Serial.println("[  OK  ] Xeros core");
    g_in_xerintosh = true;

    /* 内核初始化延迟到 loop() 第一帧，避免 setup() 累积时间
     * 触发 TG1 系统看门狗（FreeRTOS idle 任务在 setup 返回后喂狗） */
    Serial.println("[  OK  ] Hardware init complete, deferring kernel init");
}

/* ═══ 延迟内核初始化（loop() 第一帧）═══ */

static bool g_kernel_inited = false;

static void deferred_kernel_init(void)
{
    if (g_kernel_inited) return;
    g_kernel_inited = true;

    /* ── 内核子系统初始化 ── */
    kern_init();
    kern_log_set_level(KERN_LOG_INFO);
    kern_vfs_init();
    kern_devfs_init();
    kern_procfs_init();
    kern_sysfs_init();

    /* ── GPIO 文件系统 ── */
    kern_gpiofs_init();

    /* ── sysfs → 硬件双向绑定 ── */

    /* brightness: sysfs 写入时同步到 M5 屏幕背光 */
    kern_sysfs_bind(KERN_SYSFS_BRIGHTNESS,
        [](kern_sysfs_attr_t attr, int32_t val, void *ud) {
            (void)attr; (void)ud;
            uint8_t hw = (uint8_t)(val > 255 ? 255 : val);
            hal_display_set_brightness(hw);
            brightness = (int16_t)val;
            storage_set_brightness(val);
        }, NULL);

    /* rotation: sysfs 写入时同步到 M5 屏幕方向 */
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

    kern_devices_init();
    Serial.printf("[  OK  ] Kernel subsystems, free_heap=%u\n", ESP.getFreeHeap());

    /* ── 启动 Shell ── */
    kern_shell_init();
    Serial.println("[  OK  ] Shell spawned on /dev/ttyS0");

    /* ── 启动 UI 任务 ── */
    kern_pid_t ui_pid = kern_spawn("ui", ui_task_main, NULL, 4096);
    Serial.printf("[  OK  ] UI task spawned (pid=%d)\n", ui_pid);

    /* WiFi/BT 管理器作为独立内核任务运行，与 UI 任务解耦 */
    kern_spawn("wifi-mgr", wifi_mgr_task_main, NULL, 4096);
    Serial.println("[  OK  ] WiFi manager spawned as kernel task");
     kern_spawn("bt-mgr",   bt_mgr_task_main, NULL, 4096);  /* 仅状态更新，4KB 充足 */
    Serial.println("[  OK  ] BT manager spawned as kernel task");

    /* 让出 CPU 给 FreeRTOS，使刚创建的任务有机会启动并阻塞在调度信号量上 */
    delay(10);

    /* ── BT 懒加载 ──
     * BT 默认关闭（g_bt_on=false），不在此处初始化。
     * 仅当用户进入串口监视器并切换到 BLE 模式时，由 sm_app.cpp 调用
     * bt_mgr_enable() 按需初始化。这样 WiFi 启动时有充足堆内存（~163KB）。 */

    kern_log(KERN_LOG_INFO, "Xeros kernel boot complete, entering scheduler");
    Serial.println("[  OK  ] Kernel boot complete, entering scheduler loop");
}

/**
 * @brief Arduino loop()：Xeros 内核调度入口
 * @note  M5.update() 已迁移至 hal_input_update()（由 ui_task 在按键读取前调用），
 *        避免 FreeRTOS 多任务环境下跨任务边沿标志丢失。
 *        每个 kern_sched_tick() 运行一个任务切片后返回。
 */
void loop()
{
    deferred_kernel_init();


    dev_ttyS0_poll();
    serial_monitor_update();

    /* 处理 BT 启用/禁用异步请求。必须在 loop() 上下文中执行，
     * 确保 begin() / connected() / read() 在同一 FreeRTOS 任务。 */
    bt_mgr_process_requests();

    /* BT 轮询：仅在 BT 已启用时执行。
     * BluetoothSerial 内部 Bluedroid 不是线程安全的，
     * connected()/read() 已与 begin() 统一到 loop() 任务上下文。 */
    if (bt_mgr_is_enabled()) {
        static uint32_t last_bt_poll = 0;
        uint32_t now = millis();
        if (now - last_bt_poll >= 50) {
            last_bt_poll = now;
            /* DBG: 堆内存过低时告警 */
            /* LOOP-DBG 已禁用（调试 flasher 时干扰输出） */
            bt_uart_poll();
        }
    }

    kern_sched_tick();
}

#endif /* NATIVE_TEST */
