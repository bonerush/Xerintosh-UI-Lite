/**
 * @file   kern_vfs.c
 * @brief  Xeros 虚拟文件系统（VFS）实现
 * @details 实现 inode/dentry/file 三级结构、路径解析、文件描述符表、
 *          kern_open/close/read/write/ioctl 系统调用。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_vfs.h"
#include "kern_resource.h"
#include "kern_task.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ═══ 常量 ═══ */

#define MAX_DENTRY_CHILDREN 16        /* 每个 dentry 最大子节点数 */

/* ═══ 根目录项 ═══ */

static kern_dentry_t g_root_dentry;
static bool g_vfs_initialized = false;

/* ═══ 文件描述符表 ═══ */

static kern_file_t g_fd_table[KERN_MAX_FD_PER_TASK];

/* ═══ inode 引用计数管理 ═══ */

static void kfree_inode(kern_inode_t *inode)
{
    if (inode == NULL) {
        return;
    }
    /* private_data 由创建者（设备驱动/文件系统）持有，VFS 不负责释放 */
    free(inode);
}

static void kern_inode_ref(kern_inode_t *inode)
{
    if (inode == NULL) {
        return;
    }
    inode->ref_count++;
}

static void kern_inode_unref(kern_inode_t *inode)
{
    if (inode == NULL) {
        return;
    }
    if (inode->ref_count == 0) {
        return;
    }
    inode->ref_count--;
    if (inode->ref_count == 0) {
        kfree_inode(inode);
    }
}

#ifdef NATIVE_TEST
uint32_t kern_vfs_inode_ref_count(const kern_inode_t *inode)
{
    return inode != NULL ? inode->ref_count : 0;
}
#endif /* NATIVE_TEST */

/* ═══ 路径解析辅助 ═══ */

/**
 * @brief 从 root 出发，在树中逐级查找路径分量
 * @param root     起始节点
 * @param path     剩余路径（以 "/" 开头的绝对路径）
 * @param auto_create  是否自动创建中间目录（用于 kern_dentry_register）
 * @return 目标 dentry；未找到时返回 NULL
 */
static kern_dentry_t *path_walk(kern_dentry_t *root, const char *path, bool auto_create)
{
    if (path == NULL || path[0] != '/') {
        return NULL;
    }

    /* 跳过前导 "/"，直达根 */
    const char *p = path;
    while (*p == '/') p++;

    /* 路径就是 "/" 本身 */
    if (*p == '\0') {
        return root;
    }

    kern_dentry_t *cur = root;

    while (*p != '\0') {
        /* 提取下一个分量名 */
        const char *start = p;
        while (*p != '/' && *p != '\0') p++;
        size_t name_len = (size_t)(p - start);

        if (name_len == 0 || name_len > KERN_NAME_MAX) {
            return NULL;
        }

        /* 在当前节点的子节点中查找 */
        kern_dentry_t *child = NULL;
        for (uint8_t i = 0; i < cur->child_count; i++) {
            if (strncmp(cur->children[i]->name, start, name_len) == 0
                && cur->children[i]->name[name_len] == '\0') {
                child = cur->children[i];
                break;
            }
        }

        if (child == NULL) {
            if (auto_create) {
                /* 自动创建中间目录 */
                child = (kern_dentry_t *)calloc(1, sizeof(kern_dentry_t));
                if (child == NULL) return NULL;

                size_t copy_len = name_len < KERN_NAME_MAX ? name_len : KERN_NAME_MAX;
                memcpy(child->name, start, copy_len);
                child->name[copy_len] = '\0';
                child->parent = cur;
                child->child_count = 0;
                child->inode = NULL;

                if (cur->child_count >= MAX_DENTRY_CHILDREN) {
                    free(child);
                    return NULL;
                }
                cur->children[cur->child_count++] = child;
            } else {
                return NULL;
            }
        }

        cur = child;

        while (*p == '/') p++;
    }

    return cur;
}

/* ═══ 初始化 ═══ */

void kern_vfs_init(void)
{
    if (g_vfs_initialized) {
        return;
    }

    g_vfs_initialized = true;

    /* 初始化根 dentry */
    memset(&g_root_dentry, 0, sizeof(g_root_dentry));
    g_root_dentry.name[0] = '/';
    g_root_dentry.name[1] = '\0';
    g_root_dentry.parent = NULL;
    g_root_dentry.inode = NULL;
    g_root_dentry.child_count = 0;

    /* 初始化文件描述符表 */
    memset(g_fd_table, 0, sizeof(g_fd_table));
}

kern_dentry_t *kern_vfs_get_root(void)
{
    return g_vfs_initialized ? &g_root_dentry : NULL;
}

/* ═══ 目录项注册 ═══ */

kern_err_t kern_dentry_register(const char *path, kern_inode_t *inode)
{
    if (!g_vfs_initialized) return KERN_ERR;
    if (path == NULL) return KERN_EINVAL;
    if (inode == NULL) return KERN_EINVAL;

    kern_dentry_t *dentry = path_walk(&g_root_dentry, path, true);
    if (dentry == NULL) {
        return KERN_ENOENT;
    }

    /* 挂载 inode（如果已有旧 inode 则替换） */
    if (dentry->inode == inode) {
        dentry->inode->fops = inode->fops;
        return KERN_OK;
    }
    if (dentry->inode != NULL) {
        kern_inode_unref(dentry->inode);
    }
    dentry->inode = inode;
    kern_inode_ref(inode);
    dentry->inode->fops = inode->fops;

    return KERN_OK;
}

kern_dentry_t *kern_path_resolve(const char *path)
{
    if (!g_vfs_initialized) return NULL;
    if (path == NULL) return NULL;

    return path_walk(&g_root_dentry, path, false);
}

kern_err_t kern_vfs_mkdir(const char *path)
{
    if (!g_vfs_initialized) return KERN_ERR;
    if (path == NULL) return KERN_EINVAL;

    /* 若已存在则幂等返回 */
    kern_dentry_t *existing = kern_path_resolve(path);
    if (existing != NULL) {
        return KERN_OK;
    }

    kern_dentry_t *dentry = path_walk(&g_root_dentry, path, true);
    if (dentry == NULL) {
        return KERN_ENOENT;
    }

    return KERN_OK;
}

/* ─── unlink —— 删除文件或空目录 ─── */

kern_err_t kern_vfs_unlink(const char *path)
{
    if (!g_vfs_initialized) return KERN_ERR;
    if (path == NULL || path[0] != '/') return KERN_EINVAL;

    kern_dentry_t *dentry = kern_path_resolve(path);
    if (dentry == NULL) return KERN_ENOENT;

    /* 不允许删除根目录 */
    if (dentry == &g_root_dentry) return KERN_EACCES;
    if (dentry->parent == NULL) return KERN_EACCES;

    /* 非空目录不可删除 */
    if (dentry->child_count > 0) return KERN_ENOTEMPTY;

    /* 从父节点的 children 数组中移除 */
    kern_inode_t *inode = dentry->inode;
    kern_dentry_t *parent = dentry->parent;
    for (uint8_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == dentry) {
            /* 将后续元素前移 */
            for (uint8_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[parent->child_count - 1] = NULL;
            parent->child_count--;
            break;
        }
    }

    free(dentry);
    kern_inode_unref(inode);
    return KERN_OK;
}

/* ─── touch —— 创建空文件 ─── */

/* 最小化的文件操作：read 返回 EOF，write 接受一切 */
static ssize_t ramfile_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return 0;  /* EOF */
}

static ssize_t ramfile_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (ssize_t)len;  /* 接受所有写入（类似 /dev/null） */
}

static int ramfile_release(kern_file_t *f)
{
    (void)f;
    return KERN_OK;
}

static kern_file_ops_t g_ramfile_fops = {
    .read    = ramfile_read,
    .write   = ramfile_write,
    .ioctl   = NULL,
    .release = ramfile_release,
};

kern_err_t kern_vfs_touch(const char *path)
{
    if (!g_vfs_initialized) return KERN_ERR;
    if (path == NULL || path[0] != '/') return KERN_EINVAL;

    /* 若已存在则返回 EEXIST */
    kern_dentry_t *existing = kern_path_resolve(path);
    if (existing != NULL && existing->inode != NULL) {
        return KERN_EEXIST;
    }

    /* 分配 inode */
    kern_inode_t *inode = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
    if (inode == NULL) return KERN_ENOMEM;

    inode->type = KERN_FILE_REGULAR;
    inode->fops = &g_ramfile_fops;
    inode->private_data = NULL;

    int ret = kern_dentry_register(path, inode);
    if (ret != KERN_OK) {
        free(inode);
        return ret;
    }

    return KERN_OK;
}

/* ═══ 文件描述符分配 ═══ */

static kern_fd_t fd_alloc(void)
{
    for (kern_fd_t i = 0; i < KERN_MAX_FD_PER_TASK; i++) {
        if (!g_fd_table[i].in_use) {
            g_fd_table[i].in_use = true;
            return i;
        }
    }
    return KERN_EMFILE;
}

static kern_file_t *fd_get(kern_fd_t fd)
{
    if (fd < 0 || fd >= KERN_MAX_FD_PER_TASK) {
        return NULL;
    }
    if (!g_fd_table[fd].in_use) {
        return NULL;
    }
    return &g_fd_table[fd];
}

/* ═══ FD 资源释放回调 ═══ */

static void fd_release(void *ptr)
{
    kern_fd_t fd = (kern_fd_t)(intptr_t)ptr;
    if (fd >= 0) {
        kern_close(fd);
    }
}

/* ═══ 文件操作 ═══ */

kern_fd_t kern_open(const char *path, unsigned int flags)
{
    if (!g_vfs_initialized) return KERN_ERR;
    if (path == NULL) return KERN_EINVAL;

    kern_dentry_t *dentry = kern_path_resolve(path);
    if (dentry == NULL) {
        return KERN_ENOENT;
    }
    if (dentry->inode == NULL) {
        return KERN_EISDIR;  /* 打开了目录 */
    }

    kern_fd_t fd = fd_alloc();
    if (fd < 0) {
        return fd;  /* 返回错误码 */
    }

    kern_file_t *f = &g_fd_table[fd];
    f->dentry = dentry;
    f->inode = dentry->inode;
    f->fops = dentry->inode->fops;
    f->flags = flags;
    f->f_pos = 0;
    f->private_data = NULL;

    kern_inode_ref(f->inode);

    /* 调用设备的 open 回调（如果存在） */
    if (f->fops != NULL && f->fops->open != NULL) {
        int open_rc = f->fops->open(f, flags);
        if (open_rc != KERN_OK) {
            kern_inode_unref(f->inode);
            memset(f, 0, sizeof(kern_file_t));
            return open_rc;
        }
    }

    /* 追踪 FD 到当前任务 */
    kern_task_t *cur = kern_task_current();
    if (cur != NULL) {
        kern_resource_track(cur, (void *)(intptr_t)fd,
                            KERN_RES_FD, fd_release);
    }

    return fd;
}

kern_err_t kern_close(kern_fd_t fd)
{
    kern_file_t *f = fd_get(fd);
    if (f == NULL) {
        return KERN_EBADF;
    }

    /* 从资源追踪中移除 */
    kern_task_t *cur = kern_task_current();
    if (cur != NULL) {
        kern_resource_untrack(cur, (void *)(intptr_t)fd);
    }

    /* 调用设备的 release 回调 */
    if (f->fops != NULL && f->fops->release != NULL) {
        f->fops->release(f);
    }

    kern_inode_t *inode = f->inode;
    memset(f, 0, sizeof(kern_file_t));
    kern_inode_unref(inode);
    return KERN_OK;
}

ssize_t kern_read(kern_fd_t fd, char *buf, size_t len)
{
    kern_file_t *f = fd_get(fd);
    if (f == NULL) {
        return KERN_EBADF;
    }
    if (f->fops == NULL || f->fops->read == NULL) {
        return KERN_EINVAL;
    }
    if (buf == NULL) {
        return KERN_EINVAL;
    }

    return f->fops->read(f, buf, len);
}

ssize_t kern_write(kern_fd_t fd, const char *buf, size_t len)
{
    kern_file_t *f = fd_get(fd);
    if (f == NULL) {
        return KERN_EBADF;
    }
    if (f->fops == NULL || f->fops->write == NULL) {
        return KERN_EINVAL;
    }
    if (buf == NULL) {
        return KERN_EINVAL;
    }

    return f->fops->write(f, buf, len);
}

kern_err_t kern_ioctl(kern_fd_t fd, unsigned int cmd, unsigned long arg)
{
    kern_file_t *f = fd_get(fd);
    if (f == NULL) {
        return KERN_EBADF;
    }
    if (f->fops == NULL || f->fops->ioctl == NULL) {
        return KERN_EINVAL;
    }

    return f->fops->ioctl(f, cmd, arg);
}
