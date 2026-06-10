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
 *              - 两种解析器同时运行，谁先匹配就以谁为准
 *
 *          RX 噪音过滤：
 *              UART→USB 数据仅在最近 2s 内有 USB→UART 转发时才回传。
 *              进度计算只解析 USB→UART 方向的命令，不受 RX 影响。
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

/* ═══ 协议自动识别 ═══ */
typedef enum {
    PROTO_NONE = 0,              /* 尚未检测到有效协议数据 */
    PROTO_STK500,                /* 已确认 STK500 协议 (avrdude/Arduino) */
    PROTO_ESP32                  /* 已确认 ESP32 SLIP 协议 (esptool) */
} flasher_proto_t;

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

/* ═══ ESP32 SLIP 帧解析器常量 ═══ */
#define SLIP_DELIMITER     0xC0
#define SLIP_ESC           0xDB
#define SLIP_ESC_C0        0xDC
#define SLIP_ESC_DB        0xDD
#define SLIP_HDR_SIZE      24   /* 最大帧头缓冲（8B 包头 + 16B FLASH_BEGIN 载荷） */

/* ESP32 ROM Bootloader 命令码 */
#define ESP_CMD_FLASH_BEGIN  0x02
#define ESP_CMD_FLASH_DATA   0x03
#define ESP_CMD_FLASH_END    0x04
#define ESP_CMD_SYNC         0x08

/* ═══ 状态变量 ═══ */
static flasher_ui_state_t      s_ui;
static uint32_t                s_pt_tx_bytes = 0;
static uint32_t                s_pt_rx_bytes = 0;
static uint32_t                s_pt_last_tx_ms = 0;
static pt_phase_t              s_pt_phase = PT_PHASE_IDLE;
static uint32_t                s_pt_phase_until_ms = 0;
static bool                    s_pt_first_data = false;
static bool                    s_running = false;
static bool                    s_prev_landscape = true;
static float                   s_entry_offset = 0.0f;

/* STK500 进度解析器 */
static stk_parse_state_t       s_stk_state  = STK_S_IDLE;
static uint8_t                 s_stk_addr_lo = 0;
static uint16_t                s_stk_skip_rem = 0;
static uint32_t                s_stk_pages = 0;
static uint16_t                s_stk_max_addr = 0;

/* ESP32 SLIP 帧解析器 */
static uint8_t                 s_slip_buf[SLIP_HDR_SIZE];
static int                     s_slip_buf_len = 0;
static bool                    s_slip_escape = false;
static bool                    s_slip_in_frame = false;
static uint32_t                s_esp_blocks = 0;
static uint32_t                s_esp_total_blocks = 0;
static uint32_t                s_esp_total_size = 0;

/* 协议自动识别 */
static flasher_proto_t         s_proto = PROTO_NONE;

/* 全局标志：有线桥接激活时，内核 Shell 不消费 Serial 数据 */
bool g_flasher_bridge_active = false;

/* 透传调试开关 */
#define FLASHER_DBG_ENABLED 0

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>
#endif

/* ═══ STK500 进度解析器 ═══ */

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
 * @return true 若成功解析到一个完整命令（用于协议确认）
 */
static bool stk_parse_byte(uint8_t b)
{
    bool complete = false;
    switch (s_stk_state) {

    case STK_S_IDLE:
        if (b == STK_LOAD_ADDR_CMD) {
            s_stk_state = STK_S_ADDR_LO;
        } else if (b == STK_PROG_PAGE_CMD) {
            s_stk_state = STK_S_LEN_HI;
        }
        break;

    case STK_S_ADDR_LO:
        s_stk_addr_lo = b;
        s_stk_state = STK_S_ADDR_HI;
        break;

    case STK_S_ADDR_HI: {
        uint16_t addr = s_stk_addr_lo | ((uint16_t)b << 8);
        if (addr > s_stk_max_addr) s_stk_max_addr = addr;
        s_stk_state = STK_S_IDLE;
        complete = true;
        break;
    }

    case STK_S_LEN_HI:
        s_stk_addr_lo = b;
        s_stk_state = STK_S_LEN_LO;
        break;

    case STK_S_LEN_LO: {
        uint16_t len = ((uint16_t)s_stk_addr_lo << 8) | b;
        s_stk_skip_rem = 1 + len + 1;
        s_stk_state = STK_S_MODE;
        break;
    }

    case STK_S_MODE:
        if (s_stk_skip_rem > 0) s_stk_skip_rem--;
        if (s_stk_skip_rem > 0) {
            s_stk_state = STK_S_SKIP;
        } else {
            s_stk_state = STK_S_IDLE;
        }
        break;

    case STK_S_SKIP:
        if (s_stk_skip_rem > 0) s_stk_skip_rem--;
        if (s_stk_skip_rem == 0) {
            s_stk_pages++;
            s_stk_state = STK_S_IDLE;
            complete = true;
        }
        break;
    }
    return complete;
}

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

/* ═══ ESP32 SLIP 帧解析器 ═══ */

static void esp_progress_reset(void)
{
    s_slip_buf_len = 0;
    s_slip_escape = false;
    s_slip_in_frame = false;
    s_esp_blocks = 0;
    s_esp_total_blocks = 0;
    s_esp_total_size = 0;
}

/**
 * @brief 解析一个已收集的 SLIP 帧头（去掉 0xC0 后已反转义的字节）
 * @note  帧格式: [0x00] [dir] [cmd] [size_lo] [size_hi] [checksum 4B] [payload...]
 *        dir=0x00=请求, 我们只追踪 PC 发来的请求
 */
static void slip_parse_frame(void)
{
    if (s_slip_buf_len < 8) return;

    uint8_t dir = s_slip_buf[1];
    if (dir != 0x00) return;       /* 只追踪请求方向 */

    uint8_t cmd = s_slip_buf[2];

    if (cmd == ESP_CMD_FLASH_BEGIN && s_slip_buf_len >= 24) {
        /* FLASH_BEGIN payload (offset 8..23):
         *   [0..3]  total_size
         *   [4..7]  num_blocks
         *   [8..11] block_size
         *   [12..15] offset
         */
        s_esp_total_size =
            ((uint32_t)s_slip_buf[8])  | ((uint32_t)s_slip_buf[9] << 8) |
            ((uint32_t)s_slip_buf[10] << 16) | ((uint32_t)s_slip_buf[11] << 24);
        s_esp_total_blocks =
            ((uint32_t)s_slip_buf[12]) | ((uint32_t)s_slip_buf[13] << 8) |
            ((uint32_t)s_slip_buf[14] << 16) | ((uint32_t)s_slip_buf[15] << 24);
    } else if (cmd == ESP_CMD_FLASH_DATA) {
        s_esp_blocks++;
    }
    /* FLASH_END (0x04): nothing to track */
}

/**
 * @brief 向 SLIP 解析器喂入一个原始字节（来自 USB→UART 方向）
 * @return true 若成功解析到一个完整的 SLIP 帧头（用于协议确认）
 */
static bool slip_feed(uint8_t b)
{
    bool complete = false;

    if (b == SLIP_DELIMITER) {
        /* 帧定界符：结束前一帧 / 开始新帧 */
        if (s_slip_buf_len > 0) {
            slip_parse_frame();
            complete = true;
        }
        s_slip_buf_len = 0;
        s_slip_escape = false;
        s_slip_in_frame = true;
        return complete;
    }

    if (!s_slip_in_frame) return false;

    /* SLIP 转义处理 */
    if (s_slip_escape) {
        if (b == SLIP_ESC_C0) b = SLIP_DELIMITER;
        else if (b == SLIP_ESC_DB) b = SLIP_ESC;
        s_slip_escape = false;
    } else if (b == SLIP_ESC) {
        s_slip_escape = true;
        return false;
    }

    /* 反转义后的字节缓冲到帧头区域 */
    if (s_slip_buf_len < SLIP_HDR_SIZE) {
        s_slip_buf[s_slip_buf_len++] = b;
    }

    return false;
}

static int esp_get_progress(void)
{
    if (s_esp_total_blocks == 0) return 0;
    int pct = (int)((s_esp_blocks * 100) / s_esp_total_blocks);
    return (pct > 100) ? 100 : pct;
}

/* ═══ 协议自动识别 + 统一进度 ═══ */

/**
 * @brief 同时重置 STK500 和 ESP32 解析器（DTR 触发时调用）
 */
static void proto_progress_reset(void)
{
    s_proto = PROTO_NONE;
    stk_progress_reset();
    esp_progress_reset();
}

/**
 * @brief  向协议识别引擎喂入一个 USB→UART 字节
 * @note   未确认协议时双解析器同时运行；
 *         STK500 匹配到完整 LOAD_ADDR 或 PROG_PAGE 命令后锁定；
 *         ESP32 匹配到完整 SLIP 帧（含 FLASH_BEGIN/FLASH_DATA/SYNC）后锁定。
 */
static void proto_feed(uint8_t b)
{
    if (s_proto == PROTO_NONE || s_proto == PROTO_STK500) {
        if (stk_parse_byte(b) && s_proto == PROTO_NONE) {
            s_proto = PROTO_STK500;
        }
    }
    if (s_proto == PROTO_NONE || s_proto == PROTO_ESP32) {
        if (slip_feed(b) && s_slip_buf_len >= 8 && s_proto == PROTO_NONE) {
            uint8_t cmd = s_slip_buf[2];
            if (cmd == ESP_CMD_FLASH_BEGIN || cmd == ESP_CMD_FLASH_DATA ||
                cmd == ESP_CMD_SYNC) {
                s_proto = PROTO_ESP32;
            }
        }
    }
}

/**
 * @brief  根据已确认协议获取当前进度
 * @return 0-100（ESP32 优先，其次 STK500；未确认返回 0）
 */
static int proto_get_progress(void)
{
    if (s_proto == PROTO_ESP32) {
        if (s_esp_total_blocks > 0) return esp_get_progress();
    }
    if (s_proto == PROTO_STK500) {
        return stk_get_progress();
    }
    return 0;
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
    proto_progress_reset();
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
                proto_progress_reset();
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
        proto_progress_reset();
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
                proto_feed(usb_buf[i]);
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
        int pct = proto_get_progress();
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
