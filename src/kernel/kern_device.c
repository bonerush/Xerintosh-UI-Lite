/**
 * @file   kern_device.c
 * @brief  Xeros 统一设备驱动模型实现
 * @details 实现全局设备注册表（链表）、VFS 桥接层，
 *          将 kern_device_ops_t 翻译为 kern_file_ops_t。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_device.h"
#include "kern_init.h"
#include "kern_vfs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ 全局设备链表 ═══ */

static kern_device_t *g_device_list = NULL;

/* ═══ 设备注册 / 查找 ═══ */

kern_err_t kern_device_register(kern_device_t *dev)
{
    if (dev == NULL) {
        return KERN_EINVAL;
    }
    if (dev->name[0] == '\0') {
        return KERN_EINVAL;
    }

    /* 检查同名设备是否已注册 */
    kern_device_t *existing = kern_device_find(dev->name);
    if (existing != NULL) {
        /* 同一设备指针多次注册视为幂等 */
        if (existing == dev) {
            return KERN_OK;
        }
        return KERN_EEXIST;
    }

    /* 前插到链表头部 */
    dev->next = g_device_list;
    g_device_list = dev;

    /* 创建 /dev/<name> 节点 */
    char path[KERN_PATH_MAX];
    int written = snprintf(path, sizeof(path), "/dev/%s", dev->name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        g_device_list = dev->next;
        dev->next = NULL;
        return KERN_ENOSPC;
    }

    kern_inode_t *inode = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
    if (inode == NULL) {
        g_device_list = dev->next;
        dev->next = NULL;
        return KERN_ENOMEM;
    }

    inode->type = KERN_FILE_CHRDEV;
    inode->fops = kern_device_create_fops(dev);
    inode->private_data = dev;

    kern_err_t rc = kern_dentry_register(path, inode);
    if (rc != KERN_OK) {
        free(inode);
        g_device_list = dev->next;
        dev->next = NULL;
        return rc;
    }

    kern_log(KERN_LOG_INFO, "device registered: %s", path);
    return KERN_OK;
}

kern_device_t *kern_device_find(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    kern_device_t *cur = g_device_list;
    while (cur != NULL) {
        if (strcmp(cur->name, name) == 0) {
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}

/* ═══ VFS 桥接函数 ═══
 *
 * 原理：bridge 函数通过 kern_file_t→inode→private_data 获取设备指针，
 *       然后将 kern_file_ops_t 风格的调用翻译为 kern_device_ops_t。
 *
 * 所有使用新模型的设备共享同一份 bridge fops 实例。
 */

/**
 * @brief  从 kern_file_t 提取 kern_device_t*
 */
static kern_device_t *file_to_dev(kern_file_t *f)
{
    if (f == NULL || f->inode == NULL) {
        return NULL;
    }
    return (kern_device_t *)f->inode->private_data;
}

static int bridge_open(kern_file_t *f, unsigned int flags)
{
    kern_device_t *dev = file_to_dev(f);
    if (dev == NULL || dev->ops == NULL || dev->ops->open == NULL) {
        return KERN_OK;
    }
    return dev->ops->open(dev, (int)flags);
}

static ssize_t bridge_read(kern_file_t *f, char *buf, size_t len)
{
    kern_device_t *dev = file_to_dev(f);
    if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL) {
        return KERN_EINVAL;
    }
    return (ssize_t)dev->ops->read(dev, buf, len, &f->f_pos);
}

static ssize_t bridge_write(kern_file_t *f, const char *buf, size_t len)
{
    kern_device_t *dev = file_to_dev(f);
    if (dev == NULL || dev->ops == NULL || dev->ops->write == NULL) {
        return KERN_EINVAL;
    }
    return (ssize_t)dev->ops->write(dev, buf, len, &f->f_pos);
}

static int bridge_ioctl(kern_file_t *f, unsigned int cmd, unsigned long arg)
{
    kern_device_t *dev = file_to_dev(f);
    if (dev == NULL || dev->ops == NULL || dev->ops->ioctl == NULL) {
        return KERN_ENOTTY;
    }
    return dev->ops->ioctl(dev, cmd, arg);
}

static int bridge_release(kern_file_t *f)
{
    kern_device_t *dev = file_to_dev(f);
    if (dev == NULL || dev->ops == NULL || dev->ops->close == NULL) {
        return KERN_OK;
    }
    return dev->ops->close(dev);
}

/* ═══ 共享 bridge fops ═══ */

/**
 * @brief 全局唯一的 bridge 文件操作表
 * @note  所有使用新模型的设备共用此实例。
 *        设备路由通过 inode->private_data（即 kern_device_t*）完成。
 */
static kern_file_ops_t g_device_bridge_fops = {
    .open    = bridge_open,
    .read    = bridge_read,
    .write   = bridge_write,
    .ioctl   = bridge_ioctl,
    .release = bridge_release,
};

kern_file_ops_t *kern_device_create_fops(kern_device_t *dev)
{
    (void)dev;
    return &g_device_bridge_fops;
}
