#ifndef KERN_STREAM_H
#define KERN_STREAM_H

#include "kern_types.h"
#include "kern_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kern_stream {
    uint8_t          *buffer;
    size_t            capacity;
    size_t            head;
    size_t            tail;
    size_t            count;
    xeros_spinlock_t  lock;
    kern_wait_node_t *send_wait;
    kern_wait_node_t *recv_wait;
} kern_stream_t;

kern_err_t kern_stream_init(kern_stream_t *s, void *buf, size_t capacity);
size_t     kern_stream_send(kern_stream_t *s, const void *data, size_t len,
                            uint32_t timeout_ms);
size_t     kern_stream_recv(kern_stream_t *s, void *data, size_t len,
                            uint32_t timeout_ms);
size_t     kern_stream_bytes_available(kern_stream_t *s);
size_t     kern_stream_spaces_available(kern_stream_t *s);

kern_err_t kern_msg_buf_send(kern_stream_t *s, const void *msg, size_t len,
                              uint32_t timeout_ms);
size_t     kern_msg_buf_recv(kern_stream_t *s, void *msg, size_t max_len,
                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* KERN_STREAM_H */
