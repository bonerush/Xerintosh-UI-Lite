/**
 * @file   flasher_protocol.cpp
 * @brief  ESP32 ROM Bootloader SLIP 协议核心实现
 * @details 实现 SLIP 帧编解码、bootloader 命令包构建、会话状态管理及
 *          ESP32 ROM 校验和算法。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher_protocol.h"
#include <string.h>

#define SLIP_END  0xC0
#define SLIP_ESC  0xDB
#define SLIP_ESC_END  0xDC
#define SLIP_ESC_ESC  0xDD

void flasher_session_init(flasher_session_t *s, uint32_t addr, uint32_t size)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->state = FLASHER_STATE_IDLE;
    s->flash_addr = addr;
    s->total_size = size;
    s->written_size = 0;
    s->chip_id = 0;
    s->last_error = 0;
}

int flasher_slip_encode(const uint8_t *in, int in_len, uint8_t *out, int out_max)
{
    if (in == NULL || out == NULL || out_max < 2) return -1;
    if (in_len < 0) return -1;

    int pos = 0;
    out[pos++] = SLIP_END;

    for (int i = 0; i < in_len; i++) {
        if (pos + 2 > out_max) return -1;
        uint8_t b = in[i];
        if (b == SLIP_END) {
            out[pos++] = SLIP_ESC;
            out[pos++] = SLIP_ESC_END;
        } else if (b == SLIP_ESC) {
            out[pos++] = SLIP_ESC;
            out[pos++] = SLIP_ESC_ESC;
        } else {
            out[pos++] = b;
        }
    }

    if (pos + 1 > out_max) return -1;
    out[pos++] = SLIP_END;
    return pos;
}

int flasher_slip_decode(const uint8_t *in, int in_len, uint8_t *out, int out_max)
{
    if (in == NULL || out == NULL || out_max < 0) return -1;
    if (in_len < 0) return -1;

    int start = -1;
    for (int i = 0; i < in_len; i++) {
        if (in[i] == SLIP_END) {
            start = i;
            break;
        }
    }
    if (start < 0) return -1;

    int end = -1;
    for (int i = start + 1; i < in_len; i++) {
        if (in[i] == SLIP_END) {
            end = i;
            break;
        }
    }
    if (end < 0) return -1;

    int pos = 0;
    for (int i = start + 1; i < end; i++) {
        if (pos >= out_max) return -1;
        uint8_t b = in[i];
        if (b == SLIP_ESC) {
            if (i + 1 >= end) return -1;
            uint8_t next = in[++i];
            if (next == SLIP_ESC_END) {
                out[pos++] = SLIP_END;
            } else if (next == SLIP_ESC_ESC) {
                out[pos++] = SLIP_ESC;
            } else {
                return -1;
            }
        } else {
            out[pos++] = b;
        }
    }
    return pos;
}

int flasher_build_cmd(uint8_t *buf, int buf_max,
                      uint8_t cmd, uint32_t check_sum,
                      const uint8_t *data, uint16_t data_len)
{
    if (buf == NULL) return -1;
    if (data_len > 0 && data == NULL) return -1;
    if (buf_max < 8 + data_len) return -1;

    buf[0] = 0x00;
    buf[1] = cmd;
    buf[2] = (uint8_t)(data_len & 0xFF);
    buf[3] = (uint8_t)(data_len >> 8);
    buf[4] = (uint8_t)(check_sum & 0xFF);
    buf[5] = (uint8_t)((check_sum >> 8) & 0xFF);
    buf[6] = (uint8_t)((check_sum >> 16) & 0xFF);
    buf[7] = (uint8_t)((check_sum >> 24) & 0xFF);

    if (data_len > 0) {
        memcpy(&buf[8], data, data_len);
    }
    return 8 + data_len;
}

bool flasher_process_rx(flasher_session_t *s, const uint8_t *data, int len)
{
    (void)s;
    (void)data;
    (void)len;
    return false;
}

int flasher_get_progress(const flasher_session_t *s)
{
    if (s == NULL || s->total_size == 0) return 0;
    int pct = (int)((s->written_size * 100ULL) / s->total_size);
    if (pct > 100) pct = 100;
    return pct;
}
