/**
 * @file   sm_buffer.c
 * @brief  串口监视器终端缓冲区实现
 * @details 实现终端环形缓冲区的初始化、添加、读取和清空。
 *
 * @copyright Copyright (c) 2026
 */

#include "sm_buffer.h"
#include <string.h>

/* ═══ 内部辅助 ═══ */

/**
 * @brief  计算指定偏移对应的环形缓冲区索引
 * @note   offset=0 指向最新行，offset=1 指向上一行...
 */
static inline int16_t sm_buf_idx(const sm_buffer_t *buf, int16_t offset)
{
    return (buf->head - 1 - offset + SM_TERM_LINES) % SM_TERM_LINES;
}

void sm_buffer_init(sm_buffer_t *buf)
{
    memset(buf, 0, sizeof(*buf));
}

bool sm_buffer_add_line(sm_buffer_t *buf, const char *text, bool from_host)
{
    if (!buf || !text) return false;

    sm_line_t *line = &buf->lines[buf->head];
    strncpy(line->text, text, SM_TERM_LINE_LEN - 1);
    line->text[SM_TERM_LINE_LEN - 1] = '\0';
    line->from_host = from_host;

    buf->head = (buf->head + 1) % SM_TERM_LINES;
    if (buf->count < SM_TERM_LINES) {
        buf->count++;
    }
    return true;
}

void sm_buffer_get_line(const sm_buffer_t *buf, int16_t offset,
                         char *out, size_t out_len)
{
    if (!buf || !out || out_len == 0) return;

    out[0] = '\0';
    if (offset < 0 || offset >= buf->count) return;

    int16_t idx = sm_buf_idx(buf, offset);
    const sm_line_t *line = &buf->lines[idx];
    strncpy(out, line->text, out_len - 1);
    out[out_len - 1] = '\0';
}

bool sm_buffer_get_line_source(const sm_buffer_t *buf, int16_t offset)
{
    if (!buf) return false;
    if (offset < 0 || offset >= buf->count) return false;

    int16_t idx = sm_buf_idx(buf, offset);
    return buf->lines[idx].from_host;
}

void sm_buffer_clear(sm_buffer_t *buf)
{
    if (!buf) return;
    memset(buf, 0, sizeof(*buf));
}
