/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期 — 有线桥接模式
 * @details USB 串口 ↔ Serial1（G26 TX / G36 RX）双向透传，搭配 DTR（G0）
 *          自动复位 Arduino bootloader。
 *
 *          透传流程：
 *              1. 用户进入烧录器，显示全屏进度条 UI
 *              2. 长按 A 或首次 USB 数据触发 DTR 脉冲（G0 LOW 50ms）
 *              3. 等待 500ms 让 Optiboot bootloader 初始化 UART
 *              4. 进入 IDLE 阶段，USB ↔ UART 双向透传
 *              5. 用户运行 avrdude 烧录目标板
 *              6. 通过解析 USB→UART 中的 STK500 命令实时显示烧录进度
 *
 *          RX 噪音过滤：
 *              UART→USB 数据仅在最近 2s 内有 USB→UART 转发时才回传，
 *              避免 Serial1 RX 悬空噪声被误认为目标板响应。
 *              进度计算只解析 USB→UART 方向的 STK500 命令，不受 RX 影响。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
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

/* ═══ STK500 进度解析状态机 ═══ */
typedef enum {
    STK_S_IDLE = 0,       /* 等待命令字节（仅响应 0x55/0x64） */
    STK_S_ADDR_LO,        /* LOAD_ADDRESS: 等待 addr_lo */
    STK_S_ADDR_HI,        /* LOAD_ADDRESS: 等待 addr_hi */
    STK_S_LEN_HI,         /* PROG_PAGE: 等待 len_hi */
    STK_S_LEN_LO,         /* PROG_PAGE: 等待 len_lo */
    STK_S_MODE,           /* PROG_PAGE: 等待 mode ('F') */
    STK_S_SKIP            /* PROG_PAGE: 跳过 data + EOP */
} stk_parse_state_t;

#define STK_LOAD_ADDR_CMD  0x55
#define STK_PROG_PAGE_CMD  0x64
#define STK_CRC_EOP        0x20
#define STK_PROG_PAGE_SIZE 128

/* ═══ 状态变量 ═══ */
static flasher_ui_state_t      s_ui;
static uint32_t                s_pt_tx_bytes = 0;        /* USB→UART 字节数 */
static uint32_t                s_pt_rx_bytes = 0;        /* UART→USB 字节数 */
static uint32_t                s_pt_last_tx_ms = 0;      /* 最后一次 USB→UART 转发的时间戳 */
static pt_phase_t              s_pt_phase = PT_PHASE_IDLE;
static uint32_t                s_pt_phase_until_ms = 0;
static bool                    s_pt_first_data = false;  /* 是否已收到首包 USB 数据 */
static bool                    s_running = false;
static bool                    s_prev_landscape = true;
static float                   s_entry_offset = 0.0f;

/* STK500 进度解析器 */
static stk_parse_state_t       s_stk_state  = STK_S_IDLE;
static uint8_t                 s_stk_addr_lo = 0;
static uint16_t                s_stk_skip_rem = 0;
static uint32_t                s_stk_pages = 0;
static uint16_t                s_stk_max_addr = 0;

/* 全局标志：有线桥接激活时，内核 Shell 不消费 Serial 数据 */
bool g_flasher_bridge_active = false;

/* 透传调试开关 */
#define FLASHER_DBG_ENABLED 0

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>
#endif

/* ═══ STK500 进度解析器 ═══ */

/**
 * @brief 重置 STK500 进度解析器（每次新的烧录会话开始时调用）
 */
static void stk_progress_reset(void)
{
    s_stk_state  = STK_S_IDLE;
    s_stk_addr_lo = 0;
    s_stk_skip_rem = 0;
    s_stk_pages = 0;
    s_stk_max_addr = 0;
}

/**
 * @brief 向 STK500 解析器喂入一个字节（来自 USB→UART 方向）
 * @note  仅解析 LOAD_ADDRESS 和 PROG_PAGE 命令以跟踪进度。
 *        其他命令字节被忽略，解析器通过 CRC_EOP (0x20) 自然同步。
 */
static void stk_parse_byte(uint8_t b)
{
    switch (s_stk_state) {

    case STK_S_IDLE:
        if (b == STK_LOAD_ADDR_CMD) {
            s_stk_state = STK_S_ADDR_LO;
        } else if (b == STK_PROG_PAGE_CMD) {
            s_stk_state = STK_S_LEN_HI;
        }
        /* 其他字节忽略，继续等待命令字节 */
        break;

    case STK_S_ADDR_LO:
        s_stk_addr_lo = b;
        s_stk_state = STK_S_ADDR_HI;
        break;

    case STK_S_ADDR_HI: {
        uint16_t addr = s_stk_addr_lo | ((uint16_t)b << 8);
        if (addr > s_stk_max_addr) s_stk_max_addr = addr;
        s_stk_state = STK_S_IDLE;
        break;
    }

    case STK_S_LEN_HI:
        /* len_hi 暂存，下一步合成为 16-bit */
        s_stk_addr_lo = b; /* 复用暂存 */
        s_stk_state = STK_S_LEN_LO;
        break;

    case STK_S_LEN_LO: {
        uint16_t len = ((uint16_t)s_stk_addr_lo << 8) | b;
        /* skip_rem = mode(1) + data(len) + EOP(1) */
        s_stk_skip_rem = 1 + len + 1;
        s_stk_state = STK_S_MODE;
        break;
    }

    case STK_S_MODE:
        /* 消费 mode 字节（应为 'F'） */
        if (s_stk_skip_rem > 0) s_stk_skip_rem--;
        if (s_stk_skip_rem > 0) {
            s_stk_state = STK_S_SKIP;
        } else {
            /* 空页（不应出现），回到 IDLE */
            s_stk_state = STK_S_IDLE;
        }
        break;

    case STK_S_SKIP:
        if (s_stk_skip_rem > 0) s_stk_skip_rem--;
        if (s_stk_skip_rem == 0) {
            /* 最后一个字节应为 CRC_EOP (0x20)，页写入完成 */
            s_stk_pages++;
            s_stk_state = STK_S_IDLE;
        }
        break;
    }
}

/**
 * @brief  根据 STK500 解析器状态计算当前烧录进度
 * @return 进度 0-100
 * @note   进度 = 已写入页数 × 128 / max(最大字地址×2, 已写入字节数)
 *         无有效数据时返回 0
 */
static int stk_get_progress(void)
{
    if (s_stk_pages == 0) return 0;
    uint32_t written = s_stk_pages * STK_PROG_PAGE_SIZE;
    uint32_t estimated = (uint32_t)(s_stk_max_addr + 1) * 2;
    if (estimated < written) estimated = written;
    if (estimated == 0) return 0;
    int pct = (int)((written * 100) / estimated);
    return (pct > 100) ? 100 : pct;
}

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
    stk_progress_reset();
    flasher_ui_set_progress(&s_ui, 0);

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

    s_running = true;
    g_flasher_bridge_active = true;   /* 抑制 Shell 消费 Serial */
    flasher_ui_set_status(&s_ui, FLASHER_UI_BRIDGE);

#if FLASHER_DBG_ENABLED
    Serial.printf("[FLASHER-DBG] init done: wired-bridge running=%d\n", (int)s_running);
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
                Serial.printf("[FLASHER-DBG] DTR pulse (LOW 50ms)\n");
                Serial.flush();
#endif
                flasher_set_dtr(true);
                hal_delay_ms(50);
                flasher_set_dtr(false);
                s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
                s_pt_phase_until_ms = hal_get_ticks() + 500;
                /* 复位 STK500 进度解析器（新烧录会话开始） */
                stk_progress_reset();
                flasher_ui_set_progress(&s_ui, 0);
                flasher_ui_set_status(&s_ui, FLASHER_UI_FLASHING);
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

    /* ── 退出 ── */
    if (ui_user_item_try_exit(event_b)) return;

    /* ── 长按 A：手动复位（触发 DTR + 进度重置）── */
    if (event_a == HAL_EVENT_LONG_PRESS) {
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
        stk_progress_reset();
        flasher_ui_set_progress(&s_ui, 0);
        flasher_ui_set_status(&s_ui, FLASHER_UI_FLASHING);
    }

    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

    /*
     * ═══ 有线桥接：USB ↔ UART 双向透传 ═══
     *
     * USB → UART：读 PC 端 avrdude 发来的数据，转发到 Serial1（Arduino）。
     *   非 IDLE 阶段（DTR_WAIT / BOOTLOADER_WAIT）USB 数据被丢弃，
     *   避免发到正在复位或初始化 UART 的目标板。
     *   同时解析 STK500 命令以跟踪烧录进度。
     *
     * UART → USB：读 Arduino bootloader 的响应，转发回 PC。
     *   RX 噪音过滤：仅最近 2s 内有 USB→UART 转发时才回传，
     *   避免 Serial1 RX 悬空噪声被当作 Arduino 响应。
     */

    /* ── USB (PC) → UART (Arduino) ── */
    if (Serial.available()) {
        uint8_t usb_buf[64];
        int usb_len = 0;
        while (usb_len < (int)sizeof(usb_buf) && Serial.available()) {
            usb_buf[usb_len++] = Serial.read();
        }

        /* 首个 USB 数据包触发自动 DTR（若尚未触发） */
        if (!s_pt_first_data) {
            s_pt_first_data = true;
            if (s_pt_phase == PT_PHASE_IDLE) {
                s_pt_phase = PT_PHASE_DTR_WAIT;
                s_pt_phase_until_ms = hal_get_ticks() + 1;
            }
            /* 首包丢弃（板子正在复位），不转发到 UART */
        } else if (s_pt_phase == PT_PHASE_IDLE) {
            /* 转发前解析 STK500 命令以跟踪进度 */
            for (int i = 0; i < usb_len; i++) {
                stk_parse_byte(usb_buf[i]);
            }
            flasher_uart_write(usb_buf, usb_len);
            s_pt_tx_bytes += (uint32_t)usb_len;
            s_pt_last_tx_ms = hal_get_ticks();  /* 记录转发时间 */
        }
        /* 非 IDLE 阶段：数据丢弃 */
    }

    /* ── UART (Arduino) → USB (PC) ── */
    {
        uint8_t uart_buf[64];
        int uart_len = flasher_uart_read(uart_buf, sizeof(uart_buf));
        if (uart_len > 0) {
            /* RX 噪音过滤：仅最近 2s 内有 USB→UART 转发时才回传 */
            if (hal_get_ticks() - s_pt_last_tx_ms < 2000) {
                Serial.write(uart_buf, uart_len);
                Serial.flush();
            }
            s_pt_rx_bytes += (uint32_t)uart_len;
        }
    }

    /* ── 更新进度条 ── */
    {
        int pct = stk_get_progress();
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
