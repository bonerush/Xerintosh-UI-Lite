/**
 * @file   main.cpp
 * @brief  M5Stick-C 固件主入口（FreeRTOS 精简版）
 * @details 硬件环境主程序：初始化 M5Unified、NVS 存储、设置、显示驱动、
 *          UI 菜单及管理器，使用 FreeRTOS 原生任务替代 Xeros 内核。
 *
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>

#include "app/storage/storage.h"
#include "app/settings/settings.h"
#include "app/app_init.h"

extern "C" {
void wifi_mgr_task_main(void *arg);
void bt_mgr_task_main(void *arg);
}

#ifndef NATIVE_TEST

extern "C" {
bool g_wifi_on = true;  /* WiFi 默认开启（BT 默认关闭，内存充足） */
bool g_bt_on = false;   /* 蓝牙默认关，仅在串口监视器 BLE 模式时懒加载 */
}

#include <M5Unified.h>
#include <M5GFX.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
#include "app/taskmgr/taskmgr.h"

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
    M5.Display.setBrightness(hw);
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
    M5.Display.setRotation(gfx_rotation);
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
    Serial.println("\n[  BOOT] M5Stick-P1 FreeRTOS starting...");

    M5.begin();
    Serial.println("[  OK  ] M5.begin()");

    storage_init();
    Serial.println("[  OK  ] NVS storage");
    settings_load_from_storage();
    Serial.println("[  OK  ] Settings loaded from NVS");

    brightness = g_brightness_level * 10;
    M5.Display.setBrightness((uint8_t)settings_brightness_hw_value());
    g_anim_speed = settings_anim_speed_value();

    /* M5StickC 实测 rotation 效果：
     *   setRotation(0) → 正常竖屏 (portrait)
     *   setRotation(1) → 正常横屏 (landscape) */
    g_is_landscape = (g_screen_rotation_level == ORIENTATION_LANDSCAPE);
    int16_t gfx_rotation = g_is_landscape ? 1 : 0;
    M5.Display.setRotation(gfx_rotation);

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
    Serial.println("[  OK  ] UI core initialized");
    g_in_xerintosh = true;

    /* FreeRTOS 任务创建延迟到 loop() 第一帧，避免 setup() 累积时间
     * 触发 TG1 系统看门狗 */
    Serial.println("[  OK  ] Hardware init complete, deferring task creation");
}

/* ═══ 延迟任务创建（loop() 第一帧）═══ */

static bool g_tasks_started = false;

static void start_freertos_tasks(void)
{
    if (g_tasks_started) return;
    g_tasks_started = true;

    TaskHandle_t handle = NULL;

    /* ── 启动 UI 任务 ── */
    BaseType_t ret = xTaskCreatePinnedToCore(
        ui_task_main,    /* 任务函数 */
        "ui",            /* 任务名 */
        4096,            /* 栈大小（字） */
        NULL,            /* 参数 */
        1,               /* 优先级 */
        &handle,         /* 任务句柄 */
        1                /* 绑定到 Core 1 */
    );
    Serial.printf("[  OK  ] UI task created (ret=%d)\n", ret);
    if (ret == pdPASS) taskmgr_register_task(handle, "ui", true);

    /* WiFi 管理器 */
    ret = xTaskCreatePinnedToCore(
        wifi_mgr_task_main, "wifi-mgr", 4096, NULL, 1, &handle, 1);
    Serial.printf("[  OK  ] WiFi manager task created (ret=%d)\n", ret);
    if (ret == pdPASS) taskmgr_register_task(handle, "wifi-mgr", true);

    /* BT 管理器 */
    ret = xTaskCreatePinnedToCore(
        bt_mgr_task_main, "bt-mgr", 8192, NULL, 1, &handle, 1);
    Serial.printf("[  OK  ] BT manager task created (ret=%d)\n", ret);
    if (ret == pdPASS) taskmgr_register_task(handle, "bt-mgr", true);

    /* 让出 CPU 给 FreeRTOS，使刚创建的任务有机会启动 */
    delay(10);

    /* BT 懒加载：
     * BT 默认关闭（g_bt_on=false），不在此处初始化。
     * 仅当用户进入串口监视器并切换到 BLE 模式时，由 sm_app.cpp 调用
     * bt_mgr_enable() 按需初始化。 */

    Serial.println("[  OK  ] FreeRTOS boot complete, entering scheduler loop");
}

/**
 * @brief Arduino loop()：FreeRTOS 调度入口
 * @note  FreeRTOS 抢占式调度器自动在各任务间切换。
 *        loop() 处理串口监视器和 BT 轮询，每次迭代 yield 1ms
 *        以确保 idle 任务（优先级 0）能及时喂狗。
 */
void loop()
{
    start_freertos_tasks();

    serial_monitor_update();

    /* BT 轮询：仅在 BT 已启用时执行。
     * BluetoothSerial 内部的 Bluedroid 不是线程安全的，
     * connected()/read() 必须与 begin() 在同一 FreeRTOS 任务中调用。 */
    if (bt_mgr_is_enabled()) {
        static uint32_t last_bt_poll = 0;
        uint32_t now = millis();
        if (now - last_bt_poll >= 50) {
            last_bt_poll = now;
            bt_uart_poll();
        }
    }

    /* 释放 CPU 给 FreeRTOS idle 任务喂狗 */
    vTaskDelay(1);
}

#endif /* NATIVE_TEST */
