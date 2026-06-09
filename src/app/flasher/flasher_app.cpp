/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期 — 有线桥接模式
 * @details USB 串口 ↔ Serial1（G26 TX / G36 RX）双向透传，搭配 DTR（G0）
 *          自动复位 Arduino bootloader。
 *
 *          透传流程：
 *              1. 用户进入烧录器，UI 显示 WIRED BRIDGE 115200 baud
 *              2. 长按 A 或首次 USB 数据触发 DTR 脉冲（G0 LOW 50ms）
 *              3. 等待 500ms 让 Optiboot bootloader 初始化 UART
 *              4. 进入 IDLE 阶段，USB ↔ UART 双向透传
 *              5. 用户运行 avrdude 烧录目标板
 *
 *          离线烧录模式：通过 M5Stick 自身实现 STK500/SLIP 协议，
 *          不依赖 PC 端 avrdude（需预先将 hex 通过蓝牙发送到设备）。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
#include "flasher_protocol.h"
#include "flasher_protocol_stk500.h"
#include "flasher_ui.h"
#include <string.h>
#include "app/settings/settings.h"
#include "hal/hal_display.h"
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
static flasher_session_t       s_session;
static stk500_session_t        s_stk500_session;
static flasher_protocol_type_t s_protocol_type = FLASHER_PROTO_DEFAULT;
static bool                    s_pass_through = false;
static uint32_t                s_pt_tx_bytes = 0;        /* USB→UART 字节数 */
static uint32_t                s_pt_rx_bytes = 0;        /* UART→USB 字节数 */
static uint32_t                s_pt_last_tx_ms = 0;      /* 最后一次 USB→UART 转发的时间戳 */
static pt_phase_t              s_pt_phase = PT_PHASE_IDLE;
static uint32_t                s_pt_phase_until_ms = 0;
static bool                    s_pt_first_data = false;  /* 是否已收到首包 USB 数据 */
static bool                    s_running = false;
static bool                    s_prev_landscape = true;
static float                   s_entry_offset = 0.0f;

#define FW_BUF_SIZE 2048
static uint8_t s_fw_buf[FW_BUF_SIZE];
static int     s_fw_len = 0;

/* 全局标志：有线桥接激活时，内核 Shell 不消费 Serial 数据 */
bool g_flasher_bridge_active = false;

/* 透传调试开关 */
#define FLASHER_DBG_ENABLED 0

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>
#endif

/* ═══ 协议自动识别 ═══ */

static bool flasher_detect_protocol(void)
{
    /* ── 尝试 ESP32 ── */
    flasher_enter_download_mode();
    uint8_t cmd_buf[64], slip_buf[128];
    uint8_t sync_data[36] = {0x07, 0x07, 0x12, 0x20};
    memset(sync_data + 4, 0x55, 32);
    int cmd_len = flasher_build_cmd(cmd_buf, 64, FLASHER_CMD_SYNC, 0, sync_data, 36);
    int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 128);
    if (slip_len > 0) flasher_uart_write(slip_buf, slip_len);

    hal_delay_ms(50);
    uint8_t resp[256];
    if (flasher_uart_read(resp, 256) > 0) {
        s_protocol_type = FLASHER_PROTO_ESP32;
        return true;
    }

    /* ── 尝试 STK500v1 ── */
    flasher_set_boot(false);
    flasher_set_dtr(true);
    hal_delay_ms(50);
    flasher_set_dtr(false);
    hal_delay_ms(500);
    if (stk500_try_sync()) {
        s_protocol_type = FLASHER_PROTO_STK500V1;
        return true;
    }
    return false;
}

/* ═══ App 生命周期 ═══ */

void flasher_init(void *ud)
{
    (void)ud;
    s_pass_through = g_flasher_pass_through;
    s_fw_len = 0;
    s_entry_offset = (float)SCREEN_HEIGHT;
    s_protocol_type = FLASHER_PROTO_DEFAULT;
    s_pt_tx_bytes = 0;
    s_pt_rx_bytes = 0;
    s_pt_phase = PT_PHASE_IDLE;
    s_pt_phase_until_ms = 0;
    s_pt_first_data = false;

    flasher_ui_init(&s_ui);
    flasher_session_init(&s_session, 0x10000, 0);
    stk500_session_init(&s_stk500_session, 0);

    flasher_load_pin_config();
    flasher_init_pins(115200U);

#ifndef NATIVE_TEST
    s_prev_landscape = g_is_landscape;
    if (!g_is_landscape) {
        g_is_landscape = true;
        g_screen_rotation_level = ORIENTATION_LANDSCAPE;
        M5.Display.setRotation(1);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_reset_events();
    hal_input_set_double_click_enabled(false);

    if (s_pass_through) {
        s_running = true;
        g_flasher_bridge_active = true;   /* 抑制 Shell 消费 Serial */
        flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
    } else {
        s_running = false;
    }

#if FLASHER_DBG_ENABLED
    Serial.printf("[FLASHER-DBG] init done: mode=%s running=%d\n",
                  s_pass_through ? "wired-bridge" : "offline",
                  (int)s_running);
    Serial.flush();
#endif
#endif
}

#ifndef NATIVE_TEST

static void flasher_wired_draw(void)
{
    int16_t fh = hal_get_font_height();
    int16_t cy = fh + 4;

    const char *title = "WIRED BRIDGE";
    int16_t tw = hal_get_string_width(title);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, cy, title, COLOR_ACCENT);

    char buf[32];
    snprintf(buf, sizeof(buf), "115200 baud");
    tw = hal_get_string_width(buf);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, cy + fh + 4, buf, COLOR_FG);

    snprintf(buf, sizeof(buf), "TX:%lu", (unsigned long)s_pt_tx_bytes);
    hal_draw_string(4, cy + 2 * (fh + 4), buf, COLOR_FG);

    const char *phase_str;
    switch (s_pt_phase) {
        case PT_PHASE_DTR_WAIT:       phase_str = "WAIT"; break;
        case PT_PHASE_BOOTLOADER_WAIT: phase_str = "BOOT"; break;
        default:                       phase_str = "RDY";  break;
    }
    snprintf(buf, sizeof(buf), "%s", phase_str);
    tw = hal_get_string_width(buf);
    hal_draw_string(SCREEN_WIDTH / 2 - tw / 2, cy + 2 * (fh + 4), buf,
                    (s_pt_phase == PT_PHASE_IDLE) ? COLOR_ACCENT : COLOR_FG);

    snprintf(buf, sizeof(buf), "RX:%lu", (unsigned long)s_pt_rx_bytes);
    tw = hal_get_string_width(buf);
    hal_draw_string(SCREEN_WIDTH - tw - 4, cy + 2 * (fh + 4), buf, COLOR_FG);

    const char *hint = "Long-A: RST";
    tw = hal_get_string_width(hint);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, SCREEN_HEIGHT - 4, hint, COLOR_FG);
}
#endif

void flasher_loop(void *ud)
{
    (void)ud;

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* ── 透传阶段状态机 ── */
#ifndef NATIVE_TEST
    if (s_pass_through) {
        switch (s_pt_phase) {
            case PT_PHASE_DTR_WAIT:
                if (hal_get_ticks() >= s_pt_phase_until_ms) {
#if FLASHER_DBG_ENABLED
                    Serial.printf("[FLASHER-DBG] DTR pulse (LOW 50ms)\n");
                    Serial.flush();
#endif
                    flasher_set_dtr(true);
                    hal_delay_ms(50);
                    flasher_set_dtr(false);
                    s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
                    s_pt_phase_until_ms = hal_get_ticks() + 500;
#if FLASHER_DBG_ENABLED
                    Serial.printf("[FLASHER-DBG] DTR done, waiting bootloader 500ms\n");
                    Serial.flush();
#endif
                }
                break;
            case PT_PHASE_BOOTLOADER_WAIT:
                if (hal_get_ticks() >= s_pt_phase_until_ms) {
                    s_pt_phase = PT_PHASE_IDLE;
#if FLASHER_DBG_ENABLED
                    Serial.printf("[FLASHER-DBG] bootloader wait done, BRIDGE ACTIVE\n");
                    Serial.flush();
#endif
                }
                break;
            default:
                break;
        }
    }
#endif

    /* ── 退出 ── */
    if (ui_user_item_try_exit(event_b)) return;

    /* ── 长按 A：手动复位 ── */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (s_pass_through) {
#if FLASHER_DBG_ENABLED
            Serial.printf("[FLASHER-DBG] manual reset: DTR 50ms + wait 500ms\n");
            Serial.flush();
#endif
            flasher_set_dtr(true);
            hal_delay_ms(50);
            flasher_set_dtr(false);
            s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
            s_pt_phase_until_ms = hal_get_ticks() + 500;
            s_pt_first_data = true;
        } else {
            s_running = !s_running;
            if (s_running) {
                flasher_ui_set_status(&s_ui, FLASHER_UI_LOADING);
                s_fw_len = 0;
                s_session.state = FLASHER_STATE_CONNECTING;
                s_protocol_type = FLASHER_PROTO_DEFAULT;
            } else {
                flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                s_session.state = FLASHER_STATE_FAILED;
            }
        }
    }

    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

#ifndef NATIVE_TEST
    if (s_pass_through && s_running) {
        /*
         * ═══ 有线桥接：USB ↔ UART 双向透传 ═══
         *
         * USB → UART：读 PC 端 avrdude 发来的数据，转发到 Serial1（Arduino）。
         *   非 IDLE 阶段（DTR_WAIT / BOOTLOADER_WAIT）USB 数据被丢弃，
         *   避免发到正在复位或初始化 UART 的目标板。
         *
         * UART → USB：读 Arduino bootloader 的响应，转发回 PC。
         *   所有阶段都执行（包括 bootloader 初始化期间的任何输出）。
         */

        /* ── USB (PC) → UART (Arduino) ── */
        if (Serial.available()) {
            uint8_t usb_buf[64];
            int usb_len = 0;
            while (usb_len < (int)sizeof(usb_buf) && Serial.available()) {
                usb_buf[usb_len++] = Serial.read();
            }

#if FLASHER_DBG_ENABLED
            static bool s_logged_first_usb = false;
            if (!s_logged_first_usb) {
                Serial.printf("[FLASHER-DBG] first USB byte: 0x%02X len=%d phase=%d\n",
                              usb_buf[0], usb_len, (int)s_pt_phase);
                Serial.flush();
                s_logged_first_usb = true;
            }
#endif

            /* 首个 USB 数据包触发自动 DTR（若尚未触发） */
            if (!s_pt_first_data) {
                s_pt_first_data = true;
                if (s_pt_phase == PT_PHASE_IDLE) {
                    s_pt_phase = PT_PHASE_DTR_WAIT;
                    s_pt_phase_until_ms = hal_get_ticks() + 1;
#if FLASHER_DBG_ENABLED
                    Serial.printf("[FLASHER-DBG] auto DTR from first USB data\n");
                    Serial.flush();
#endif
                }
#if FLASHER_DBG_ENABLED
                Serial.printf("[FLASHER-DBG] dropping first USB packet (%d bytes) phase=%d\n",
                              usb_len, (int)s_pt_phase);
                Serial.flush();
#endif
                /* 首包丢弃（板子正在复位），不转发到 UART */
            } else if (s_pt_phase == PT_PHASE_IDLE) {
                flasher_uart_write(usb_buf, usb_len);
                s_pt_tx_bytes += (uint32_t)usb_len;
                s_pt_last_tx_ms = hal_get_ticks();  /* 记录转发时间 */
#if FLASHER_DBG_ENABLED
                if ((s_pt_tx_bytes & 0x3F) == 0) {
                    Serial.printf("[FLASHER-DBG] fwd USB→UART: total_tx=%lu\n",
                                  (unsigned long)s_pt_tx_bytes);
                    Serial.flush();
                }
#endif
            }
            /* 非 IDLE 阶段：数据丢弃 */
        }

        /* ── UART (Arduino) → USB (PC) ── */
        {
            uint8_t uart_buf[64];
            int uart_len = flasher_uart_read(uart_buf, sizeof(uart_buf));
            if (uart_len > 0) {
                /* 仅在最近 2 秒内有 USB→UART 转发时才回传，
                 * 避免 Serial1 RX 悬空噪声被当作 Arduino 响应 */
                if (hal_get_ticks() - s_pt_last_tx_ms < 2000) {
                    Serial.write(uart_buf, uart_len);
                    Serial.flush();
                }
                s_pt_rx_bytes += (uint32_t)uart_len;
#if FLASHER_DBG_ENABLED
                static bool s_logged_first_uart = false;
                if (!s_logged_first_uart) {
                    Serial.printf("[FLASHER-DBG] first UART→USB byte: 0x%02X len=%d\n",
                                  uart_buf[0], uart_len);
                    Serial.flush();
                    s_logged_first_uart = true;
                }
#endif
            }
        }
    }
#endif

    /* ── 离线模式：协议状态机 ── */
#ifndef NATIVE_TEST
    if (s_running && !s_pass_through) {
        switch (s_session.state) {
            case FLASHER_STATE_CONNECTING: {
                if (s_protocol_type == FLASHER_PROTO_AUTO) {
                    if (!flasher_detect_protocol()) {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                        break;
                    }
                }
                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    s_session.state = FLASHER_STATE_FLASH_BEGIN;
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    if (stk500_enter_progmode()) {
                        stk500_session_init(&s_stk500_session, 0);
                        s_session.state = FLASHER_STATE_FLASH_BEGIN;
                    } else {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                    }
                }
                break;
            }
            case FLASHER_STATE_FLASH_BEGIN: {
                if (s_fw_len >= 4) {
                    uint32_t fw_size = (uint32_t)s_fw_buf[0] |
                                      ((uint32_t)s_fw_buf[1] << 8) |
                                      ((uint32_t)s_fw_buf[2] << 16) |
                                      ((uint32_t)s_fw_buf[3] << 24);
                    memmove(s_fw_buf, s_fw_buf + 4, s_fw_len - 4);
                    s_fw_len -= 4;

                    if (s_protocol_type == FLASHER_PROTO_ESP32) {
                        s_session.total_size = fw_size;
                        s_session.written_size = 0;
                        uint8_t cmd_buf[32], slip_buf[64];
                        uint8_t begin_data[16] = {0};
                        uint32_t num_blocks = (fw_size + FLASHER_FLASH_BLOCK_SIZE - 1) / FLASHER_FLASH_BLOCK_SIZE;
                        begin_data[0] = (uint8_t)(fw_size & 0xFF);
                        begin_data[1] = (uint8_t)((fw_size >> 8) & 0xFF);
                        begin_data[2] = (uint8_t)((fw_size >> 16) & 0xFF);
                        begin_data[3] = (uint8_t)((fw_size >> 24) & 0xFF);
                        begin_data[4] = (uint8_t)(num_blocks & 0xFF);
                        begin_data[5] = (uint8_t)((num_blocks >> 8) & 0xFF);
                        begin_data[6] = (uint8_t)(FLASHER_FLASH_BLOCK_SIZE & 0xFF);
                        begin_data[7] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 8) & 0xFF);
                        begin_data[8] = (uint8_t)((s_session.flash_addr >> 8) & 0xFF);
                        begin_data[9] = (uint8_t)((s_session.flash_addr >> 16) & 0xFF);
                        begin_data[10] = (uint8_t)((s_session.flash_addr >> 24) & 0xFF);
                        int cmd_len = flasher_build_cmd(cmd_buf, 32, FLASHER_CMD_FLASH_BEGIN, 0, begin_data, 16);
                        int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 64);
                        if (slip_len > 0) flasher_uart_write(slip_buf, slip_len);
                        delay(50);
                        s_session.state = FLASHER_STATE_FLASH_DATA;
                    } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                        s_stk500_session.total_size = fw_size;
                        s_stk500_session.written_size = 0;
                        s_stk500_session.current_addr = 0;
                        s_session.state = FLASHER_STATE_FLASH_DATA;
                    }
                }
                break;
            }
            case FLASHER_STATE_FLASH_DATA: {
                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    while (s_session.written_size < s_session.total_size &&
                           s_fw_len >= FLASHER_FLASH_BLOCK_SIZE) {
                        uint8_t block[FLASHER_FLASH_BLOCK_SIZE];
                        memcpy(block, s_fw_buf, FLASHER_FLASH_BLOCK_SIZE);
                        memmove(s_fw_buf, s_fw_buf + FLASHER_FLASH_BLOCK_SIZE, s_fw_len - FLASHER_FLASH_BLOCK_SIZE);
                        s_fw_len -= FLASHER_FLASH_BLOCK_SIZE;
                        uint32_t cs = 0xEF;
                        for (int i = 0; i < FLASHER_FLASH_BLOCK_SIZE; i++) cs ^= block[i];
                        uint8_t cmd_buf[16], slip_cmd[32];
                        uint8_t data_hdr[16] = {0};
                        data_hdr[0] = (uint8_t)(FLASHER_FLASH_BLOCK_SIZE & 0xFF);
                        data_hdr[1] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 8) & 0xFF);
                        data_hdr[2] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 16) & 0xFF);
                        data_hdr[3] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 24) & 0xFF);
                        data_hdr[4] = (uint8_t)(s_session.written_size & 0xFF);
                        data_hdr[5] = (uint8_t)((s_session.written_size >> 8) & 0xFF);
                        data_hdr[6] = (uint8_t)((s_session.written_size >> 16) & 0xFF);
                        data_hdr[7] = (uint8_t)((s_session.written_size >> 24) & 0xFF);
                        int cmd_len = flasher_build_cmd(cmd_buf, 16, FLASHER_CMD_FLASH_DATA, cs, data_hdr, 16);
                        int slip_cmd_len = flasher_slip_encode(cmd_buf, cmd_len, slip_cmd, 32);
                        if (slip_cmd_len > 0) flasher_uart_write(slip_cmd, slip_cmd_len);
                        uint8_t slip_data[FLASHER_FLASH_BLOCK_SIZE + 16];
                        int slip_data_len = flasher_slip_encode(block, FLASHER_FLASH_BLOCK_SIZE, slip_data, sizeof(slip_data));
                        if (slip_data_len > 0) flasher_uart_write(slip_data, slip_data_len);
                        s_session.written_size += FLASHER_FLASH_BLOCK_SIZE;
                        flasher_ui_set_progress(&s_ui, flasher_get_progress(&s_session));
                        delay(10);
                    }
                    if (s_session.written_size >= s_session.total_size)
                        s_session.state = FLASHER_STATE_FLASH_END;
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    while (s_stk500_session.written_size < s_stk500_session.total_size &&
                           s_fw_len >= STK500_FLASH_PAGE_SIZE) {
                        uint8_t page[STK500_FLASH_PAGE_SIZE];
                        memcpy(page, s_fw_buf, STK500_FLASH_PAGE_SIZE);
                        memmove(s_fw_buf, s_fw_buf + STK500_FLASH_PAGE_SIZE, s_fw_len - STK500_FLASH_PAGE_SIZE);
                        s_fw_len -= STK500_FLASH_PAGE_SIZE;
                        uint16_t word_addr = s_stk500_session.current_addr;
                        if (!stk500_load_address(word_addr) ||
                            !stk500_program_page(page, STK500_FLASH_PAGE_SIZE)) {
                            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                            s_session.state = FLASHER_STATE_FAILED;
                            break;
                        }
                        s_stk500_session.current_addr += (STK500_FLASH_PAGE_SIZE / 2);
                        s_stk500_session.written_size += STK500_FLASH_PAGE_SIZE;
                        flasher_ui_set_progress(&s_ui, stk500_get_progress(&s_stk500_session));
                    }
                    if (s_session.state != FLASHER_STATE_FAILED &&
                        s_stk500_session.written_size < s_stk500_session.total_size &&
                        s_fw_len > 0 && s_fw_len < STK500_FLASH_PAGE_SIZE) {
                        uint8_t page[STK500_FLASH_PAGE_SIZE];
                        uint16_t remain = s_fw_len;
                        memcpy(page, s_fw_buf, remain);
                        s_fw_len = 0;
                        if (!stk500_load_address(s_stk500_session.current_addr) ||
                            !stk500_program_page(page, remain)) {
                            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                            s_session.state = FLASHER_STATE_FAILED;
                        } else {
                            s_stk500_session.written_size = s_stk500_session.total_size;
                        }
                    }
                    if (s_stk500_session.written_size >= s_stk500_session.total_size)
                        s_session.state = FLASHER_STATE_FLASH_END;
                }
                break;
            }
            case FLASHER_STATE_FLASH_END: {
                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    uint8_t cmd_buf[16], slip_buf[32];
                    uint8_t end_data[4] = {0};
                    int cmd_len = flasher_build_cmd(cmd_buf, 16, FLASHER_CMD_FLASH_END, 0, end_data, 4);
                    int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 32);
                    if (slip_len > 0) flasher_uart_write(slip_buf, slip_len);
                    flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
                    s_session.state = FLASHER_STATE_DONE;
                    flasher_reset_target();
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    if (stk500_leave_progmode()) {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
                        flasher_set_dtr(true);
                        hal_delay_ms(50);
                        flasher_set_dtr(false);
                        hal_delay_ms(100);
                        s_session.state = FLASHER_STATE_DONE;
                    } else {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
#endif

    /* ── 绘制 ── */
    if (s_pass_through) {
#ifndef NATIVE_TEST
        flasher_wired_draw();
#endif
    } else {
        flasher_ui_draw(&s_ui);
    }
}

void flasher_exit(void *ud)
{
    (void)ud;
    s_running = false;
    g_flasher_bridge_active = false;  /* 恢复 Shell 串口输入 */

#ifndef NATIVE_TEST
    if (!s_prev_landscape) {
        g_is_landscape = false;
        g_screen_rotation_level = ORIENTATION_PORTRAIT;
        M5.Display.setRotation(0);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_set_double_click_enabled(false);
    hal_input_reset_events();
#endif
}
