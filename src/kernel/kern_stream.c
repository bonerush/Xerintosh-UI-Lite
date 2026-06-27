#include "kern_stream.h"
#include "kern_sched.h"
#include "kern_task.h"

#include <string.h>

kern_err_t kern_stream_init(kern_stream_t *s, void *buf, size_t capacity)
{
    if (s == NULL || buf == NULL || capacity == 0) return KERN_EINVAL;
    s->buffer = (uint8_t *)buf;
    s->capacity = capacity;
    s->head = 0;
    s->tail = 0;
    s->count = 0;
    xeros_spinlock_init(&s->lock);
    s->send_wait = NULL;
    s->recv_wait = NULL;
    return KERN_OK;
}

static size_t stream_write(kern_stream_t *s, const uint8_t *data, size_t len)
{
    size_t written = 0;
    while (written < len && s->count < s->capacity) {
        s->buffer[s->tail] = data[written];
        s->tail = (s->tail + 1) % s->capacity;
        s->count++;
        written++;
    }
    return written;
}

static size_t stream_read(kern_stream_t *s, uint8_t *data, size_t len)
{
    size_t read = 0;
    while (read < len && s->count > 0) {
        data[read] = s->buffer[s->head];
        s->head = (s->head + 1) % s->capacity;
        s->count--;
        read++;
    }
    return read;
}

size_t kern_stream_send(kern_stream_t *s, const void *data, size_t len,
                        uint32_t timeout_ms)
{
    if (s == NULL || data == NULL || len == 0) return 0;

    size_t total = 0;
    uint32_t deadline = (timeout_ms > 0) ? (g_sched_ticks + timeout_ms) : 0;
    const uint8_t *src = (const uint8_t *)data;

    while (total < len) {
        xeros_spinlock_lock(&s->lock);
        size_t n = stream_write(s, src + total, len - total);
        if (n > 0) {
            ipc_wake_one(&s->recv_wait);
        }
        xeros_spinlock_unlock(&s->lock);

        total += n;
        if (total >= len) break;
        if (timeout_ms == 0) break;
        if (g_sched_ticks >= deadline) break;

        uint32_t remain = (deadline > g_sched_ticks) ? (deadline - g_sched_ticks) : 0;
        if (remain == 0) break;
        ipc_block_task(&s->send_wait, &s->lock, remain);
    }
    return total;
}

size_t kern_stream_recv(kern_stream_t *s, void *data, size_t len,
                        uint32_t timeout_ms)
{
    if (s == NULL || data == NULL || len == 0) return 0;

    size_t total = 0;
    uint32_t deadline = (timeout_ms > 0) ? (g_sched_ticks + timeout_ms) : 0;
    uint8_t *dst = (uint8_t *)data;

    while (total < len) {
        xeros_spinlock_lock(&s->lock);
        size_t n = stream_read(s, dst + total, len - total);
        if (n > 0) {
            ipc_wake_one(&s->send_wait);
        }
        xeros_spinlock_unlock(&s->lock);

        total += n;
        if (total >= len) break;
        if (timeout_ms == 0) break;
        if (g_sched_ticks >= deadline) break;

        uint32_t remain = (deadline > g_sched_ticks) ? (deadline - g_sched_ticks) : 0;
        if (remain == 0) break;
        ipc_block_task(&s->recv_wait, &s->lock, remain);
    }
    return total;
}

size_t kern_stream_bytes_available(kern_stream_t *s)
{
    if (s == NULL) return 0;
    xeros_spinlock_lock(&s->lock);
    size_t c = s->count;
    xeros_spinlock_unlock(&s->lock);
    return c;
}

size_t kern_stream_spaces_available(kern_stream_t *s)
{
    if (s == NULL) return 0;
    xeros_spinlock_lock(&s->lock);
    size_t sp = s->capacity - s->count;
    xeros_spinlock_unlock(&s->lock);
    return sp;
}

kern_err_t kern_msg_buf_send(kern_stream_t *s, const void *msg, size_t len,
                              uint32_t timeout_ms)
{
    if (s == NULL || msg == NULL || len > 0xFFFF) return KERN_EINVAL;

    uint8_t header[2];
    header[0] = (uint8_t)(len & 0xFF);
    header[1] = (uint8_t)((len >> 8) & 0xFF);

    if (kern_stream_send(s, header, 2, timeout_ms) != 2) return KERN_ENOSPC;
    if (kern_stream_send(s, msg, len, timeout_ms) != len) return KERN_ENOSPC;
    return KERN_OK;
}

size_t kern_msg_buf_recv(kern_stream_t *s, void *msg, size_t max_len,
                          uint32_t timeout_ms)
{
    if (s == NULL || msg == NULL) return 0;

    uint8_t header[2];
    if (kern_stream_recv(s, header, 2, timeout_ms) != 2) return 0;

    size_t len = (size_t)header[0] | ((size_t)header[1] << 8);
    if (len > max_len) {
        uint8_t tmp;
        for (size_t i = 0; i < len; i++) {
            kern_stream_recv(s, &tmp, 1, 0);
        }
        return 0;
    }

    return kern_stream_recv(s, msg, len, timeout_ms);
}
