/**
 * @file   flasher_proto.c
 * @brief  烧录器协议自动识别引擎 — STK500 / ESP32 SLIP 协议解析器
 * @details 从 USB→UART 方向数据流中自动检测 avrdude (STK500) 和 esptool (ESP32 SLIP)
 *          协议，内部维护双解析器状态机并计算烧录进度。所有状态为 static，无堆分配。
 *
 *          协议自动识别：
 *              - STK500: 检测到 0x55 (LOAD_ADDR) 或 0x64 (PROG_PAGE) 后确认
 *              - ESP32 SLIP: 检测到 0xC0 帧头 + FLASH_BEGIN(0x02) 或 SYNC(0x08) 后确认
 *              - 两种解析器同时运行，谁先匹配就以谁为准
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_proto.h"
#include <stdbool.h>
#include <string.h>

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

/* ═══ 内部状态 ═══ */
static flasher_proto_t          s_proto = FLASHER_PROTO_NONE;

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

/* ═══ 协议自动识别 + 统一进度（公开接口） ═══ */

void flasher_proto_reset(void)
{
    s_proto = FLASHER_PROTO_NONE;
    stk_progress_reset();
    esp_progress_reset();
}

flasher_proto_t flasher_proto_feed(uint8_t b)
{
    if (s_proto == FLASHER_PROTO_NONE || s_proto == FLASHER_PROTO_STK500) {
        if (stk_parse_byte(b) && s_proto == FLASHER_PROTO_NONE) {
            s_proto = FLASHER_PROTO_STK500;
        }
    }
    if (s_proto == FLASHER_PROTO_NONE || s_proto == FLASHER_PROTO_ESP32) {
        if (slip_feed(b) && s_slip_buf_len >= 8 && s_proto == FLASHER_PROTO_NONE) {
            uint8_t cmd = s_slip_buf[2];
            if (cmd == ESP_CMD_FLASH_BEGIN || cmd == ESP_CMD_FLASH_DATA ||
                cmd == ESP_CMD_SYNC) {
                s_proto = FLASHER_PROTO_ESP32;
            }
        }
    }
    return s_proto;
}

int flasher_proto_get_progress(void)
{
    if (s_proto == FLASHER_PROTO_ESP32) {
        if (s_esp_total_blocks > 0) return esp_get_progress();
    }
    if (s_proto == FLASHER_PROTO_STK500) {
        return stk_get_progress();
    }
    return 0;
}
