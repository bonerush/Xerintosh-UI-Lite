# 流缓冲区与消息缓冲区

> **Parent:** [Xeros 内核文档](index.md)

流缓冲区提供字节流 FIFO，支持阻塞发送/接收与超时；消息缓冲区在其之上封装长度前缀，实现按消息边界传输。

## 核心 API

*📄 Source: [kern_stream.h](../../src/kernel/kern_stream.h#L22-L33)*

```c
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
```

## 关键实现

底层为环形缓冲区，使用 `head/tail/count` 索引，配合 `send_wait` / `recv_wait` 等待队列。

*📄 Source: [kern_stream.c](../../src/kernel/kern_stream.c#L21-L43)*

```c
static size_t stream_write(kern_stream_t *s, const uint8_t *data, size_t len)
{ ... }
static size_t stream_read(kern_stream_t *s, uint8_t *data, size_t len)
{ ... }
```

发送不足时阻塞在 `send_wait` 队列，直到接收方消费后唤醒。

*📄 Source: [kern_stream.c](../../src/kernel/kern_stream.c#L45-L72)*

```c
size_t kern_stream_send(kern_stream_t *s, const void *data, size_t len,
                        uint32_t timeout_ms)
{ ... }
```

消息缓冲区使用 2 字节小端长度前缀。

*📄 Source: [kern_stream.c](../../src/kernel/kern_stream.c#L121-L153)*

```c
kern_err_t kern_msg_buf_send(kern_stream_t *s, const void *msg, size_t len,
                              uint32_t timeout_ms)
{ ... }
```

## 使用示例

```c
uint8_t buf[128];
kern_stream_t s;
kern_stream_init(&s, buf, sizeof(buf));

uint8_t msg[] = "hello";
kern_stream_send(&s, msg, sizeof(msg), 100);

uint8_t out[16];
kern_stream_recv(&s, out, sizeof(msg), 100);
```

## 注意事项

- `timeout_ms = 0` 表示非阻塞，立即返回实际传输字节数。
- 消息缓冲区单条消息最大长度 65535 字节。

---

> **See Also:** [IPC 原语](../architecture/ipc-primitives.md)
