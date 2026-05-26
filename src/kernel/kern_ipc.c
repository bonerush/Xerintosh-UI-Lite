/**
 * @file   kern_ipc.c
 * @brief  Xeros IPC 机制实现
 * @details 实现匿名 pipe（环形缓冲区）和命名消息队列（单向链表）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_ipc.h"
#include "kern_vfs.h"

#include <string.h>
#include <stdlib.h>

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

/* ═══ Message Queue 内部结构 ═══ */

typedef struct mq_msg {
    uint8_t         type;
    size_t          len;
    char            data[KERN_MQ_MSG_SIZE];
    struct mq_msg  *next;
} mq_msg_t;

typedef struct mq_queue {
    char        name[KERN_MQ_NAME_MAX + 1];
    mq_msg_t   *head;           /* 消息链表头 */
    mq_msg_t   *tail;           /* 消息链表尾（O(1) 追加） */
    int         msg_count;      /* 当前消息数 */
    int         ref_count;      /* 引用计数（打开的 fd 数） */
    bool        in_use;
} mq_queue_t;

static mq_queue_t g_mq_queues[KERN_MQ_MAX_QUEUES];
static bool       g_mq_initialized = false;

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

/* ═══ Message Queue 内部实现 ═══ */

static void mq_init_once(void)
{
    if (g_mq_initialized) return;
    memset(g_mq_queues, 0, sizeof(g_mq_queues));
    g_mq_initialized = true;
}

static mq_queue_t *mq_find_by_name(const char *name)
{
    for (int i = 0; i < KERN_MQ_MAX_QUEUES; i++) {
        if (g_mq_queues[i].in_use
            && strcmp(g_mq_queues[i].name, name) == 0) {
            return &g_mq_queues[i];
        }
    }
    return NULL;
}

static mq_queue_t *mq_alloc(const char *name)
{
    for (int i = 0; i < KERN_MQ_MAX_QUEUES; i++) {
        if (!g_mq_queues[i].in_use) {
            mq_queue_t *q = &g_mq_queues[i];
            memset(q, 0, sizeof(mq_queue_t));
            strncpy(q->name, name, KERN_MQ_NAME_MAX);
            q->name[KERN_MQ_NAME_MAX] = '\0';
            q->in_use = true;
            return q;
        }
    }
    return NULL;
}

static void mq_free_msgs(mq_queue_t *q)
{
    mq_msg_t *cur = q->head;
    while (cur != NULL) {
        mq_msg_t *next = cur->next;
        free(cur);
        cur = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->msg_count = 0;
}

/* ═══ MQ 文件操作 ═══ */

static int mq_release(kern_file_t *f)
{
    mq_queue_t *q = (mq_queue_t *)f->private_data;
    if (q == NULL) return KERN_OK;

    q->ref_count--;
    if (q->ref_count <= 0) {
        q->ref_count = 0;
    }
    return KERN_OK;
}

static kern_file_ops_t g_mq_fops = {
    .read    = NULL,
    .write   = NULL,
    .ioctl   = NULL,
    .release = mq_release,
};

/* ═══ Message Queue 公共接口 ═══ */

kern_fd_t kern_mq_open(const char *name)
{
    if (name == NULL) return KERN_EINVAL;

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > KERN_MQ_NAME_MAX) return KERN_EINVAL;

    mq_init_once();

    mq_queue_t *q = mq_find_by_name(name);
    if (q == NULL) {
        q = mq_alloc(name);
        if (q == NULL) return KERN_ENOMEM;
    }

    kern_fd_t fd = kern_vfs_fd_create(&g_mq_fops, KERN_O_RDWR, q);
    if (fd >= 0) {
        q->ref_count++;
    }
    return fd;
}

int kern_mq_send(kern_fd_t fd, uint8_t type, const void *data, size_t len)
{
    if (data == NULL && len > 0) return KERN_EINVAL;
    if (len > KERN_MQ_MSG_SIZE) return KERN_EINVAL;

    mq_queue_t *q = (mq_queue_t *)kern_vfs_fd_get_private(fd);
    if (q == NULL) return KERN_EBADF;

    if (q->msg_count >= KERN_MQ_MAX_MSGS) {
        return KERN_ENOSPC;
    }

    mq_msg_t *msg = (mq_msg_t *)calloc(1, sizeof(mq_msg_t));
    if (msg == NULL) return KERN_ENOMEM;

    msg->type = type;
    msg->len = len;
    if (len > 0) {
        memcpy(msg->data, data, len);
    }
    msg->next = NULL;

    if (q->tail == NULL) {
        q->head = msg;
        q->tail = msg;
    } else {
        q->tail->next = msg;
        q->tail = msg;
    }
    q->msg_count++;

    return KERN_OK;
}

ssize_t kern_mq_recv(kern_fd_t fd, uint8_t type, void *data, size_t len)
{
    (void)len;

    mq_queue_t *q = (mq_queue_t *)kern_vfs_fd_get_private(fd);
    if (q == NULL) return KERN_EBADF;

    mq_msg_t *prev = NULL;
    mq_msg_t *cur = q->head;

    while (cur != NULL) {
        if (type == 0xFF || cur->type == type) {
            if (prev == NULL) {
                q->head = cur->next;
            } else {
                prev->next = cur->next;
            }
            if (q->tail == cur) {
                q->tail = prev;
            }

            size_t copy_len = cur->len;
            if (data != NULL && copy_len > 0) {
                memcpy(data, cur->data, copy_len);
            }

            ssize_t result = (ssize_t)cur->len;
            free(cur);
            q->msg_count--;

            /* 引用计数归零时清理队列 */
            if (q->msg_count == 0 && q->ref_count == 0) {
                q->in_use = false;
            }

            return result;
        }
        prev = cur;
        cur = cur->next;
    }

    return 0;
}

int kern_mq_close(kern_fd_t fd)
{
    mq_queue_t *q = (mq_queue_t *)kern_vfs_fd_get_private(fd);
    int rc = kern_close(fd);  /* 内部调用 mq_release，递减 ref_count */

    /* kern_close 后 q 指针可能悬空，但 ref_count 已在 release 中递减 */
    if (q != NULL && rc == KERN_OK) {
        /* 若无消息且无引用，释放队列 */
        if (q->ref_count <= 0 && q->msg_count == 0) {
            mq_free_msgs(q);
            q->in_use = false;
        }
    }

    return rc;
}
