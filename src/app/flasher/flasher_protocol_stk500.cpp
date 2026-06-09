/**
 * @file   flasher_protocol_stk500.cpp
 * @brief  STK500v1 协议实现（Arduino Optiboot 兼容）
 * @details 实现 STK500v1 协议的底层命令：同步、进入/离开编程模式、
 *          加载地址、编程页。使用 flasher_gpio 层的 UART 接口通信。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_protocol_stk500.h"
#include "flasher_gpio.h"
#include "hal/hal_system.h"
#include <string.h>

/* ═══ 内部辅助函数 ═══ */

/**
 * @brief 清空 UART 接收缓冲区
 */
static void stk500_clear_rx(void)
{
    uint8_t dummy[16];
    while (flasher_uart_read(dummy, 16) > 0) {}
}

/**
 * @brief 等待 STK500 标准响应（STK_INSYNC + STK_OK）
 * @param timeout_ms 超时时间（毫秒）
 * @return true 收到正确响应
 */
static bool stk500_wait_response(uint32_t timeout_ms)
{
    uint8_t buf[2];
    uint32_t start = hal_get_ticks();
    int n = 0;

    while ((uint32_t)(hal_get_ticks() - start) < timeout_ms) {
        n += flasher_uart_read(buf + n, 2 - n);
        if (n >= 2) break;
        hal_delay_ms(1);
    }

    return (n >= 2 && buf[0] == STK_INSYNC && buf[1] == STK_OK);
}

/* ═══ 会话生命周期 ═══ */

void stk500_session_init(stk500_session_t *s, uint32_t size)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->state        = STK500_STATE_IDLE;
    s->total_size   = size;
    s->written_size = 0;
    s->current_addr = 0;
    s->last_error   = 0;
}

/* ═══ 底层命令接口 ═══ */

bool stk500_try_sync(void)
{
    uint8_t cmd[2] = {STK_GET_SYNC, CRC_EOP};
    uint8_t resp[2];

    /* 参考 AVRDUDE / STK500.cpp：发送 3 次 sync + drain，
       清空线路噪声并确保 bootloader 进入同步状态 */
    for (int i = 0; i < 2; i++) {
        stk500_clear_rx();
        flasher_uart_write(cmd, 2);
        hal_delay_ms(10);
        stk500_clear_rx();
    }

    stk500_clear_rx();
    flasher_uart_write(cmd, 2);
    return stk500_wait_response(STK500_SYNC_TIMEOUT_MS);
}

bool stk500_enter_progmode(void)
{
    stk500_clear_rx();

    uint8_t cmd[2] = {STK_ENTER_PROGMODE, CRC_EOP};
    flasher_uart_write(cmd, 2);

    hal_delay_ms(50);
    return stk500_wait_response(STK500_CMD_TIMEOUT_MS);
}

bool stk500_load_address(uint16_t word_addr)
{
    stk500_clear_rx();

    uint8_t cmd[4] = {
        STK_LOAD_ADDRESS,
        (uint8_t)(word_addr & 0xFF),
        (uint8_t)(word_addr >> 8),
        CRC_EOP
    };
    flasher_uart_write(cmd, 4);

    hal_delay_ms(10);
    return stk500_wait_response(STK500_CMD_TIMEOUT_MS);
}

/* 静态页缓冲区，用于最后一页填充 */
static uint8_t s_page_buf[STK500_FLASH_PAGE_SIZE];

bool stk500_program_page(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > STK500_FLASH_PAGE_SIZE) {
        return false;
    }

    stk500_clear_rx();

    /* ATmega328P Optiboot 期望每页恰好 128 字节；
       不足时以 0xFF（已擦除 Flash 值）填充 */
    if (len < STK500_FLASH_PAGE_SIZE) {
        memcpy(s_page_buf, data, len);
        memset(s_page_buf + len, 0xFF, STK500_FLASH_PAGE_SIZE - len);
        data = s_page_buf;
        len  = STK500_FLASH_PAGE_SIZE;
    }

    /* 命令头：PROG_PAGE + 长度高字节 + 长度低字节 + 'F'(Flash) */
    uint8_t hdr[4] = {
        STK_PROG_PAGE,
        (uint8_t)((len >> 8) & 0xFF),
        (uint8_t)(len & 0xFF),
        'F'
    };
    flasher_uart_write(hdr, 4);
    flasher_uart_write(data, len);

    uint8_t eop = CRC_EOP;
    flasher_uart_write(&eop, 1);

    hal_delay_ms(20);
    return stk500_wait_response(STK500_CMD_TIMEOUT_MS);
}

bool stk500_leave_progmode(void)
{
    stk500_clear_rx();

    uint8_t cmd[2] = {STK_LEAVE_PROGMODE, CRC_EOP};
    flasher_uart_write(cmd, 2);

    hal_delay_ms(50);
    return stk500_wait_response(STK500_CMD_TIMEOUT_MS);
}

/* ═══ 进度计算 ═══ */

int stk500_get_progress(const stk500_session_t *s)
{
    if (s == NULL || s->total_size == 0) return 0;
    int pct = (int)((s->written_size * 100ULL) / s->total_size);
    if (pct > 100) pct = 100;
    return pct;
}
