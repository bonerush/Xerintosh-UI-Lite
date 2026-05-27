/**
 * @file   kern_sysfs.c
 * @brief  Xeros sysfs 实现
 * @details 实现 /sys 虚拟文件系统：为每个系统配置参数注册一个虚拟文件，
 *          通过 read/write 操作读取和修改内部值。使用属性表 + 公共 fops
 *          分派模式。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sysfs.h"
#include "kern_vfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ═══ 属性枚举 ═══ */

typedef enum {
    KERN_SYSFS_BRIGHTNESS  = 0,
    KERN_SYSFS_ROTATION    = 1,
    KERN_SYSFS_ANIM_SPEED  = 2,
    KERN_SYSFS_ANIM_ENABLED = 3,
    KERN_SYSFS_LOG_LEVEL   = 4,
    KERN_SYSFS_ATTR_COUNT  = 5
} kern_sysfs_attr_t;

/* ═══ 属性定义表 ═══ */

typedef struct {
    kern_sysfs_attr_t id;
    const char *name;
    int32_t *value_ptr;
    int32_t min_val;
    int32_t max_val;
} kern_sysfs_attr_def_t;

/* ═══ 内部值（静态全局） ═══ */

static int32_t g_sys_brightness   = 255;
static int32_t g_sys_rotation     = 0;
static int32_t g_sys_anim_speed   = 92;
static int32_t g_sys_anim_enabled = 1;
static int32_t g_sys_log_level    = 1;

/* ═══ 属性表 ═══ */

static kern_sysfs_attr_def_t g_sysfs_attrs[KERN_SYSFS_ATTR_COUNT] = {
    { KERN_SYSFS_BRIGHTNESS,   "brightness",   &g_sys_brightness,    0,   255 },
    { KERN_SYSFS_ROTATION,     "rotation",     &g_sys_rotation,      0,   3   },
    { KERN_SYSFS_ANIM_SPEED,   "anim_speed",   &g_sys_anim_speed,    0,   100 },
    { KERN_SYSFS_ANIM_ENABLED, "anim_enabled", &g_sys_anim_enabled,  0,   1   },
    { KERN_SYSFS_LOG_LEVEL,    "log_level",    &g_sys_log_level,     0,   3   },
};

/* ═══ inode 数组 ═══ */

static kern_inode_t g_sysfs_inodes[KERN_SYSFS_ATTR_COUNT];

/* ═══ 文件操作 ═══ */

static ssize_t sysfs_read(kern_file_t *f, char *buf, size_t len);
static ssize_t sysfs_write(kern_file_t *f, const char *buf, size_t len);

static kern_file_ops_t g_sysfs_fops = {
    .read    = sysfs_read,
    .write   = sysfs_write,
    .ioctl   = NULL,
    .release = NULL,
};

/**
 * @brief sysfs 读：将当前值格式化为字符串返回
 */
static ssize_t sysfs_read(kern_file_t *f, char *buf, size_t len)
{
    /* sysfs 文件内容为单行整数，读取一次后返回 EOF */
    if (f->f_pos > 0) {
        return 0;
    }

    /* 从 inode 的 private_data 获取属性索引 */
    int attr_idx = (int)(intptr_t)f->inode->private_data;
    if (attr_idx < 0 || attr_idx >= KERN_SYSFS_ATTR_COUNT) {
        return KERN_EINVAL;
    }

    const kern_sysfs_attr_def_t *def = &g_sysfs_attrs[attr_idx];

    /* 格式化为字符串 */
    char tmp[16];
    int n = snprintf(tmp, sizeof(tmp), "%d", (int)*def->value_ptr);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return KERN_ERR;
    }

    /* 拷贝到用户缓冲区 */
    size_t copy_len = (size_t)n < len ? (size_t)n : len;
    memcpy(buf, tmp, copy_len);

    f->f_pos += copy_len;
    return (ssize_t)copy_len;
}

/**
 * @brief sysfs 写：解析字符串为整数、校验范围、更新值
 */
static ssize_t sysfs_write(kern_file_t *f, const char *buf, size_t len)
{
    /* 从 inode 的 private_data 获取属性索引 */
    int attr_idx = (int)(intptr_t)f->inode->private_data;
    if (attr_idx < 0 || attr_idx >= KERN_SYSFS_ATTR_COUNT) {
        return KERN_EINVAL;
    }

    const kern_sysfs_attr_def_t *def = &g_sysfs_attrs[attr_idx];

    if (buf == NULL || len == 0) {
        return KERN_EINVAL;
    }

    /* 拷贝输入到以 null 结尾的临时缓冲区 */
    char tmp[16];
    size_t copy_len = len < (sizeof(tmp) - 1) ? len : (sizeof(tmp) - 1);
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    /* 解析为整数 */
    char *endptr = NULL;
    long val = strtol(tmp, &endptr, 10);

    /* 检查解析是否成功 */
    if (endptr == tmp) {
        return KERN_EINVAL;  /* 不是有效的数字 */
    }

    /* 范围校验 */
    if (val < (long)def->min_val || val > (long)def->max_val) {
        return KERN_EINVAL;  /* 值超出范围 */
    }

    /* 更新内部值 */
    *def->value_ptr = (int32_t)val;

    return (ssize_t)len;
}

/* ═══ 初始化 ═══ */

void kern_sysfs_init(void)
{
    /* 确保 VFS 已初始化 */
    kern_vfs_init();

    /* 重置所有值为默认值（每次 init 提供干净状态） */
    g_sys_brightness   = 255;
    g_sys_rotation     = 0;
    g_sys_anim_speed   = 92;
    g_sys_anim_enabled = 1;
    g_sys_log_level    = 1;

    /* 创建目录结构 */
    kern_vfs_mkdir("/sys");
    kern_vfs_mkdir("/sys/kernel");

    /* 初始化并注册每个属性的 inode */
    for (int i = 0; i < KERN_SYSFS_ATTR_COUNT; i++) {
        kern_sysfs_attr_def_t *def = &g_sysfs_attrs[i];
        kern_inode_t *ino = &g_sysfs_inodes[i];

        ino->type         = KERN_FILE_REGULAR;
        ino->fops         = &g_sysfs_fops;
        ino->private_data = (void *)(intptr_t)def->id;

        /* 构建路径：log_level 在 /sys/kernel/ 下，其余在 /sys/ 下 */
        char path[KERN_PATH_MAX];
        if (def->id == KERN_SYSFS_LOG_LEVEL) {
            snprintf(path, sizeof(path), "/sys/kernel/%s", def->name);
        } else {
            snprintf(path, sizeof(path), "/sys/%s", def->name);
        }

        kern_dentry_register(path, ino);
    }
}

/* ═══ Getter/Setter ═══ */

int32_t kern_sysfs_get_brightness(void)
{
    return g_sys_brightness;
}

void kern_sysfs_set_brightness(int32_t val)
{
    if (val >= 0 && val <= 255) {
        g_sys_brightness = val;
    }
}

int32_t kern_sysfs_get_rotation(void)
{
    return g_sys_rotation;
}

void kern_sysfs_set_rotation(int32_t val)
{
    if (val >= 0 && val <= 3) {
        g_sys_rotation = val;
    }
}

int32_t kern_sysfs_get_anim_speed(void)
{
    return g_sys_anim_speed;
}

void kern_sysfs_set_anim_speed(int32_t val)
{
    if (val >= 0 && val <= 100) {
        g_sys_anim_speed = val;
    }
}

int32_t kern_sysfs_get_anim_enabled(void)
{
    return g_sys_anim_enabled;
}

void kern_sysfs_set_anim_enabled(int32_t val)
{
    if (val >= 0 && val <= 1) {
        g_sys_anim_enabled = val;
    }
}

int32_t kern_sysfs_get_log_level(void)
{
    return g_sys_log_level;
}

void kern_sysfs_set_log_level(int32_t val)
{
    if (val >= 0 && val <= 3) {
        g_sys_log_level = val;
    }
}
