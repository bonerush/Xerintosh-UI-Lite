/**
 * @file   kern_devfs.c
 * @brief  Xeros 设备文件系统（devfs）实现
 * @details 实现 kern_devfs_init()，仅负责创建 /dev 目录。
 *          设备注册统一由 kern_device_register() 完成。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_devfs.h"
#include "kern_vfs.h"
#include "kern_init.h"

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
