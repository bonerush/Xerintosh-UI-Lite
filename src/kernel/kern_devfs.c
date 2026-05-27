/**
 * @file   kern_devfs.c
 * @brief  Xeros 设备文件系统（devfs）实现
 * @details 实现 kern_devfs_init() 和 kern_dev_register()，
 *          将设备挂载到 VFS 的 /dev/ 路径下。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_devfs.h"
#include "kern_init.h"

#include <stdlib.h>
#include <stdio.h>

/* ═══ 内部状态 ═══ */

static bool g_devfs_initialized = false;

/* ═══ 初始化 ═══ */

void kern_devfs_init(void)
{
    if (g_devfs_initialized) {
        return;
    }

    /* 确保 VFS 先初始化 */
    kern_vfs_init();

    /* 创建 /dev 目录 */
    kern_vfs_mkdir("/dev");

    g_devfs_initialized = true;
    kern_log(KERN_LOG_INFO, "devfs initialized");
}

/* ═══ 设备注册 ═══ */

int kern_dev_register(const char *name, kern_file_ops_t *fops,
                      kern_file_type_t type, void *private_data)
{
    if (!g_devfs_initialized) {
        return KERN_ERR;
    }
    if (name == NULL) {
        return KERN_EINVAL;
    }
    if (fops == NULL) {
        return KERN_EINVAL;
    }

    /* 构建完整路径 "/dev/<name>" */
    char path[KERN_PATH_MAX];
    int written = snprintf(path, sizeof(path), "/dev/%s", name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return KERN_ENOSPC;
    }

    /* 分配 inode */
    kern_inode_t *inode = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
    if (inode == NULL) {
        return KERN_ENOMEM;
    }

    inode->type = type;
    inode->fops = fops;
    inode->private_data = private_data;

    int rc = kern_dentry_register(path, inode);
    if (rc != KERN_OK) {
        free(inode);
        return rc;
    }

    kern_log(KERN_LOG_INFO, "device registered: %s", path);
    return KERN_OK;
}
