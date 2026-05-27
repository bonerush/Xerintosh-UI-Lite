/**
 * @file   kern_sysfs.c
 * @brief  Xeros sysfs 实现
 * @details 实现 /sys 虚拟文件系统：为每个系统配置参数注册一个虚拟文件，
 *          通过 read/write 操作读取和修改内部值。使用属性表 + 公共 fops
 *          分派模式。
 *
 *          支持双向绑定：写入触发的硬件回调（kern_sysfs_bind），
 *          以及外部同步（kern_sysfs_update，不触发回调）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sysfs.h"
#include "kern_vfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ═══ 回调链（链表） ═══ */

typedef struct sysfs_callback_node {
    kern_sysfs_change_callback_t callback;
    void                       *user_data;
    struct sysfs_callback_node *next;
} sysfs_callback_node_t;

#define MAX_CALLBACKS_PER_ATTR 4

static sysfs_callback_node_t g_callback_pool[MAX_CALLBACKS_PER_ATTR * KERN_SYSFS_ATTR_COUNT];
static int g_callback_pool_idx = 0;
static sysfs_callback_node_t *g_callback_heads[KERN_SYSFS_ATTR_COUNT]; /* 各属性的回调链表头 */

/* ═══ 属性定义表 ═══ */

typedef struct {
    kern_sysfs_attr_t id;
    const char       *name;
    int32_t          *value_ptr;
    int32_t           min_val;
    int32_t           max_val;
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
    if (f->f_pos > 0) {
        return 0;
    }

    int attr_idx = (int)(intptr_t)f->inode->private_data;
    if (attr_idx < 0 || attr_idx >= KERN_SYSFS_ATTR_COUNT) {
        return KERN_EINVAL;
    }

    const kern_sysfs_attr_def_t *def = &g_sysfs_attrs[attr_idx];

    char tmp[16];
    int n = snprintf(tmp, sizeof(tmp), "%d", (int)*def->value_ptr);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return KERN_ERR;
    }

    size_t copy_len = (size_t)n < len ? (size_t)n : len;
    memcpy(buf, tmp, copy_len);

    f->f_pos += (int32_t)copy_len;
    return (ssize_t)copy_len;
}

/**
 * @brief 通知所有绑定的回调
 */
static void sysfs_notify_callbacks(kern_sysfs_attr_t attr, int32_t new_value)
{
    sysfs_callback_node_t *node = g_callback_heads[attr];
    while (node != NULL) {
        if (node->callback != NULL) {
            node->callback(attr, new_value, node->user_data);
        }
        node = node->next;
    }
}

/**
 * @brief sysfs 写：解析字符串为整数、校验范围、更新值、触发回调
 */
static ssize_t sysfs_write(kern_file_t *f, const char *buf, size_t len)
{
    int attr_idx = (int)(intptr_t)f->inode->private_data;
    if (attr_idx < 0 || attr_idx >= KERN_SYSFS_ATTR_COUNT) {
        return KERN_EINVAL;
    }

    const kern_sysfs_attr_def_t *def = &g_sysfs_attrs[attr_idx];

    if (buf == NULL || len == 0) {
        return KERN_EINVAL;
    }

    char tmp[16];
    size_t copy_len = len < (sizeof(tmp) - 1) ? len : (sizeof(tmp) - 1);
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *endptr = NULL;
    long val = strtol(tmp, &endptr, 10);

    if (endptr == tmp) {
        return KERN_EINVAL;
    }

    if (val < (long)def->min_val || val > (long)def->max_val) {
        return KERN_EINVAL;
    }

    *def->value_ptr = (int32_t)val;

    /* 触发硬件同步回调 */
    sysfs_notify_callbacks((kern_sysfs_attr_t)attr_idx, (int32_t)val);

    return (ssize_t)len;
}

/* ═══ 初始化 ═══ */

void kern_sysfs_init(void)
{
    kern_vfs_init();

    g_sys_brightness   = 255;
    g_sys_rotation     = 0;
    g_sys_anim_speed   = 92;
    g_sys_anim_enabled = 1;
    g_sys_log_level    = 1;

    /* 清零回调链表 */
    memset(g_callback_heads, 0, sizeof(g_callback_heads));
    memset(g_callback_pool, 0, sizeof(g_callback_pool));
    g_callback_pool_idx = 0;

    kern_vfs_mkdir("/sys");
    kern_vfs_mkdir("/sys/kernel");

    for (int i = 0; i < KERN_SYSFS_ATTR_COUNT; i++) {
        kern_sysfs_attr_def_t *def = &g_sysfs_attrs[i];
        kern_inode_t *ino = &g_sysfs_inodes[i];

        ino->type         = KERN_FILE_REGULAR;
        ino->fops         = &g_sysfs_fops;
        ino->private_data = (void *)(intptr_t)def->id;

        char path[KERN_PATH_MAX];
        if (def->id == KERN_SYSFS_LOG_LEVEL) {
            snprintf(path, sizeof(path), "/sys/kernel/%s", def->name);
        } else {
            snprintf(path, sizeof(path), "/sys/%s", def->name);
        }

        kern_dentry_register(path, ino);
    }
}

/* ═══ 绑定 API ═══ */

int kern_sysfs_bind(kern_sysfs_attr_t attr,
                   kern_sysfs_change_callback_t cb,
                   void *user_data)
{
    if (cb == NULL) return KERN_EINVAL;
    if (attr >= KERN_SYSFS_ATTR_COUNT) return KERN_EINVAL;
    if (g_callback_pool_idx >= (int)(sizeof(g_callback_pool) / sizeof(g_callback_pool[0]))) {
        return KERN_ENOSPC;
    }

    sysfs_callback_node_t *node = &g_callback_pool[g_callback_pool_idx++];
    node->callback  = cb;
    node->user_data = user_data;
    node->next      = g_callback_heads[attr];
    g_callback_heads[attr] = node;

    return KERN_OK;
}

void kern_sysfs_update(kern_sysfs_attr_t attr, int32_t value)
{
    if (attr >= KERN_SYSFS_ATTR_COUNT) return;

    const kern_sysfs_attr_def_t *def = &g_sysfs_attrs[attr];
    if (value < def->min_val || value > def->max_val) return;

    *def->value_ptr = value;
    /* 不触发回调：由调用者负责硬件更新 */
}

/* ═══ Getter/Setter ═══ */

int32_t kern_sysfs_get_brightness(void)  { return g_sys_brightness; }
void    kern_sysfs_set_brightness(int32_t val)
{
    if (val >= 0 && val <= 255) g_sys_brightness = val;
}

int32_t kern_sysfs_get_rotation(void)    { return g_sys_rotation; }
void    kern_sysfs_set_rotation(int32_t val)
{
    if (val >= 0 && val <= 3) g_sys_rotation = val;
}

int32_t kern_sysfs_get_anim_speed(void)  { return g_sys_anim_speed; }
void    kern_sysfs_set_anim_speed(int32_t val)
{
    if (val >= 0 && val <= 100) g_sys_anim_speed = val;
}

int32_t kern_sysfs_get_anim_enabled(void) { return g_sys_anim_enabled; }
void    kern_sysfs_set_anim_enabled(int32_t val)
{
    if (val >= 0 && val <= 1) g_sys_anim_enabled = val;
}

int32_t kern_sysfs_get_log_level(void)   { return g_sys_log_level; }
void    kern_sysfs_set_log_level(int32_t val)
{
    if (val >= 0 && val <= 3) g_sys_log_level = val;
}
