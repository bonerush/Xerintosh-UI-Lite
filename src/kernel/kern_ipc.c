/**
 * @file   kern_ipc.c
 * @brief  Xeros IPC 机制实现
 * @details 实现匿名 pipe（环形缓冲区）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_ipc.h"
#include "kern_vfs.h"

#include <string.h>

/* ═══ Pipe 内部结构 ═══ */

typedef struct pipe_ring {
    uint8_t  buf[KERN_PIPE_BUF_SIZE];
    int      head;           /* 写入位置 */
    int      tail;           /* 读取位置 */
    int      count;          /* 缓冲区中字节数 */
    bool     read_closed;    /* 读端是否已关闭 */
    bool     write_closed;   /* 写端是否已关闭 */
    bool     in_use;         /* 槽位是否被占用 */
} pipe_ring_t;

static pipe_ring_t g_pipes[KERN_PIPE_MAX];

/* ═══ Pipe 辅助 ═══ */

static pipe_ring_t *pipe_alloc(void)
{
    for (int i = 0; i < KERN_PIPE_MAX; i++) {
        if (!g_pipes[i].in_use) {
            memset(&g_pipes[i], 0, sizeof(pipe_ring_t));
            g_pipes[i].in_use = true;
            return &g_pipes[i];
        }
    }
    return NULL;
}

static void pipe_free(pipe_ring_t *p)
{
    if (p != NULL) {
        p->in_use = false;
    }
}

/* ═══ Pipe 读写操作（内部 fops） ═══ */

static ssize_t pipe_read(kern_file_t *f, char *buf, size_t len)
{
    pipe_ring_t *p = (pipe_ring_t *)f->private_data;
    if (p == NULL) return KERN_EINVAL;

    if (p->count == 0) {
        return 0;
    }

    size_t total = 0;
    while (total < len && p->count > 0) {
        buf[total++] = (char)p->buf[p->tail];
        p->tail = (p->tail + 1) % KERN_PIPE_BUF_SIZE;
        p->count--;
    }
    return (ssize_t)total;
}

static ssize_t pipe_write(kern_file_t *f, const char *buf, size_t len)
{
    pipe_ring_t *p = (pipe_ring_t *)f->private_data;
    if (p == NULL) return KERN_EINVAL;

    if (p->read_closed) {
        return KERN_EPIPE;
    }

    size_t total = 0;
    while (total < len) {
        if (p->count >= KERN_PIPE_BUF_SIZE) {
            break;
        }
        p->buf[p->head] = (uint8_t)buf[total++];
        p->head = (p->head + 1) % KERN_PIPE_BUF_SIZE;
        p->count++;
    }
    return (ssize_t)total;
}

static int pipe_release(kern_file_t *f)
{
    pipe_ring_t *p = (pipe_ring_t *)f->private_data;
    if (p == NULL) return KERN_OK;

    if (f->flags & KERN_O_WRONLY) {
        p->write_closed = true;
    } else if (f->flags & KERN_O_RDONLY) {
        p->read_closed = true;
    }

    /* 读/写端都关闭时释放槽位 */
    if (p->read_closed && p->write_closed) {
        pipe_free(p);
    }

    return KERN_OK;
}

static kern_file_ops_t g_pipe_rd_fops = {
    .read    = pipe_read,
    .write   = NULL,
    .ioctl   = NULL,
    .release = pipe_release,
};

static kern_file_ops_t g_pipe_wr_fops = {
    .read    = NULL,
    .write   = pipe_write,
    .ioctl   = NULL,
    .release = pipe_release,
};

/* ═══ Pipe 公共接口 ═══ */

int kern_pipe(kern_fd_t fds[2])
{
    if (fds == NULL) return KERN_EINVAL;

    pipe_ring_t *p = pipe_alloc();
    if (p == NULL) return KERN_ENOMEM;

    kern_fd_t r_fd = kern_vfs_fd_create(&g_pipe_rd_fops, KERN_O_RDONLY, p);
    if (r_fd < 0) {
        pipe_free(p);
        return r_fd;
    }

    kern_fd_t w_fd = kern_vfs_fd_create(&g_pipe_wr_fops, KERN_O_WRONLY, p);
    if (w_fd < 0) {
        p->read_closed = true;
        kern_close(r_fd);
        return w_fd;
    }

    fds[0] = r_fd;
    fds[1] = w_fd;
    return KERN_OK;
}
