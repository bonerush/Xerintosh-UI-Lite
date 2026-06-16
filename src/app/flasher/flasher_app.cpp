/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期 — 有线桥接模式
 * @details USB 串口 ↔ Serial1（G26 TX / G36 RX）双向透传，搭配 DTR（G0）
 *          自动复位目标板 bootloader。
 *
 *          透传流程：
 *              1. 用户进入烧录器，显示全屏进度条 UI ("BRIDGE...")
 *              2. 长按 A 或首次 USB 数据触发 DTR 脉冲（G0 LOW 50ms）
 *              3. 等待 500ms 让 bootloader 初始化 UART
 *              4. 进入 IDLE 阶段，USB ↔ UART 双向透传
 *              5. 用户运行 avrdude / esptool 烧录目标板
 *              6. 自动识别 STK500/ESP32 SLIP 协议，实时显示烧录进度
 *
 *          协议自动识别：
 *              - STK500: 检测到 0x55 (LOAD_ADDR) 或 0x64 (PROG_PAGE) 后确认
 *              - ESP32 SLIP: 检测到 0xC0 帧头 + FLASH_BEGIN(0x02) 或 SYNC(0x08) 后确认
 *              - STM32 USART: 检测到 0x7F 自动波特率 或 0x31 0xCE (Write Memory) 后确认
 *              - 三种解析器同时运行，谁先匹配就以谁为准
 *
 *          RX 噪音过滤：
 *              UART→USB 数据仅在最近 2s 内有 USB→UART 转发时才回传。
 *              进度计算只解析 USB→UART 方向的命令，不受 RX 影响。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
#include "flasher_proto.h"
#include "flasher_ui.h"
#include "app/ui_service.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_selector.h"
#include "ui/ui_item.h"

/* ═══ 透传阶段 ═══ */
typedef enum {
    PT_PHASE_IDLE = 0,           /* 正常双向透传 */
    PT_PHASE_DTR_WAIT,           /* 等待执行 DTR 脉冲 */
    PT_PHASE_BOOTLOADER_WAIT     /* DTR 已完成，等待目标 bootloader 就绪 */
} pt_phase_t;

/* ═══ 状态变量 ═══ */
static flasher_ui_state_t      s_ui;
static uint32_t                s_pt_tx_bytes = 0;
static uint32_t                s_pt_rx_bytes = 0;
static uint32_t                s_pt_last_tx_ms = 0;
static pt_phase_t              s_pt_phase = PT_PHASE_IDLE;
static uint32_t                s_pt_phase_until_ms = 0;
static bool                    s_pt_first_data = false;
static bool                    s_running = false;
static float                   s_entry_offset = 0.0f;

/* 全局标志：有线桥接激活时，内核 Shell 不消费 Serial 数据 */
bool g_flasher_bridge_active = false;

/* 透传调试开关 */
#define FLASHER_DBG_ENABLED 0

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif


/* ═══ App 生命周期 ═══ */

void flasher_init(void *ud)
{
    (void)ud;
    s_pt_tx_bytes = 0;
    s_pt_rx_bytes = 0;
    s_pt_phase = PT_PHASE_IDLE;
    s_pt_phase_until_ms = 0;
    s_pt_first_data = false;
    s_entry_offset = (float)SCREEN_HEIGHT;

    flasher_ui_init(&s_ui);
    flasher_proto_reset();
    flasher_ui_set_progress(&s_ui, 0);

    flasher_load_pin_config();
    flasher_init_pins(115200U);

#ifndef NATIVE_TEST
    ui_service_enter_landscape();
    ui_service_user_item_init();
    hal_input_set_double_click_enabled(false);

    s_running = true;
    g_flasher_bridge_active = true;
    flasher_ui_set_status(&s_ui, FLASHER_UI_BRIDGE);

#if FLASHER_DBG_ENABLED
    Serial.printf("[FLASHER] init done: wired-bridge running=%d\n", (int)s_running);
    Serial.flush();
#endif
#endif
}

#ifndef NATIVE_TEST

void flasher_loop(void *ud)
{
    (void)ud;

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* ── 透传阶段状态机 ── */
    switch (s_pt_phase) {
        case PT_PHASE_DTR_WAIT:
            if (hal_get_ticks() >= s_pt_phase_until_ms) {
#if FLASHER_DBG_ENABLED
                Serial.printf("[FLASHER] DTR pulse (LOW 50ms)\n");
                Serial.flush();
#endif
                flasher_set_dtr(true);
                hal_delay_ms(50);
                flasher_set_dtr(false);
                s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
                s_pt_phase_until_ms = hal_get_ticks() + 500;
                flasher_proto_reset();
                flasher_ui_set_progress(&s_ui, 0);
                flasher_ui_set_status(&s_ui, FLASHER_UI_FLASHING);
#if FLASHER_DBG_ENABLED
                Serial.printf("[FLASHER] DTR done, waiting bootloader 500ms\n");
                Serial.flush();
#endif
            }
            break;
        case PT_PHASE_BOOTLOADER_WAIT:
            if (hal_get_ticks() >= s_pt_phase_until_ms) {
                s_pt_phase = PT_PHASE_IDLE;
#if FLASHER_DBG_ENABLED
                Serial.printf("[FLASHER] bootloader wait done, BRIDGE ACTIVE\n");
                Serial.flush();
#endif
            }
            break;
        default:
            break;
    }

    /* ── 退出 ── */
    if (ui_user_item_try_exit(event_b)) return;

    /* ── 长按 A：手动复位 ── */
    if (event_a == HAL_EVENT_LONG_PRESS) {
#if FLASHER_DBG_ENABLED
        Serial.printf("[FLASHER] manual reset: DTR 50ms + wait 500ms\n");
        Serial.flush();
#endif
        flasher_set_dtr(true);
        hal_delay_ms(50);
        flasher_set_dtr(false);
        s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
        s_pt_phase_until_ms = hal_get_ticks() + 500;
        s_pt_first_data = true;
        flasher_proto_reset();
        flasher_ui_set_progress(&s_ui, 0);
        flasher_ui_set_status(&s_ui, FLASHER_UI_FLASHING);
    }

    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

    /*
     * ═══ 有线桥接：USB ↔ UART 双向透传 ═══
     */

    /* ── USB (PC) → UART (目标板) ── */
    if (Serial.available()) {
        uint8_t usb_buf[64];
        int usb_len = 0;
        while (usb_len < (int)sizeof(usb_buf) && Serial.available()) {
            usb_buf[usb_len++] = Serial.read();
        }

        if (!s_pt_first_data) {
            s_pt_first_data = true;
            if (s_pt_phase == PT_PHASE_IDLE) {
                s_pt_phase = PT_PHASE_DTR_WAIT;
                s_pt_phase_until_ms = hal_get_ticks() + 1;
            }
        } else if (s_pt_phase == PT_PHASE_IDLE) {
            /* 协议自动识别 + 进度解析 */
            for (int i = 0; i < usb_len; i++) {
                flasher_proto_feed(usb_buf[i]);
            }
            flasher_uart_write(usb_buf, usb_len);
            s_pt_tx_bytes += (uint32_t)usb_len;
            s_pt_last_tx_ms = hal_get_ticks();
        }
    }

    /* ── UART (目标板) → USB (PC) ── */
    {
        uint8_t uart_buf[64];
        int uart_len = flasher_uart_read(uart_buf, sizeof(uart_buf));
        if (uart_len > 0) {
            if (hal_get_ticks() - s_pt_last_tx_ms < 2000) {
                Serial.write(uart_buf, uart_len);
                Serial.flush();
            }
            s_pt_rx_bytes += (uint32_t)uart_len;
        }
    }

    /* ── 更新进度条 ── */
    {
        int pct = flasher_proto_get_progress();
        if (pct > 0) {
            flasher_ui_set_progress(&s_ui, pct);
            if (pct >= 100) {
                flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
            }
        }
    }

    /* ── 绘制全屏进度条 UI ── */
    flasher_ui_draw(&s_ui);
}

#else /* NATIVE_TEST */

void flasher_loop(void *ud) { (void)ud; }

#endif

void flasher_exit(void *ud)
{
    (void)ud;
    s_running = false;
    g_flasher_bridge_active = false;

#ifndef NATIVE_TEST
    ui_service_exit_landscape();
    ui_service_user_item_exit();
#endif
}
