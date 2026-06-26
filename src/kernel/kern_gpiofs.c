/**
 * @file   kern_gpiofs.c
 * @brief  Xeros GPIO 虚拟文件系统实现
 * @details 将 M5Stick-C 的 GPIO 引脚状态映射到 /sys/gpio 文件系统。
 *          每个引脚对应一个可读文件（输出引脚也可写）。
 *          /sys/gpio/list 提供所有引脚的汇总表。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_gpiofs.h"
#include "kern_vfs.h"
#include "kern_init.h"
#include "kern_types.h"
#include "kern_sync.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef NATIVE_TEST
#include "driver/gpio.h"
#endif

/* ═══ 内部常量 ═══ */

#define GPIOFS_CONTENT_MAX  256
#define GPIOFS_MAX_PINS     16
#define GPIOFS_PIN_MAX      40  /* ESP32 有效 GPIO 编号 0-39 */

/* ═══ 引脚描述符 ═══ */

typedef struct {
    uint8_t     pin;        /* GPIO 编号 */
    const char *func;       /* 功能描述 */
    bool        can_output; /* 是否支持输出 */
} gpiofs_pin_desc_t;

/**
 * @brief M5Stick-C GPIO 引脚表
 */
static const gpiofs_pin_desc_t g_gpio_pins[] = {
    {  0, "BOOT / I2C SDA",    true  },
    { 25, "Speaker DAC",       true  },
    { 26, "LCD Backlight (HAL only)", false },
    { 32, "Grove SDA",         true  },
    { 33, "Grove SCL",         true  },
    { 36, "Button A (RTC)",    false },
    { 37, "Button B (RTC)",    false },
};

#define GPIOFS_PIN_COUNT (sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))

/* ═══ 内部状态 ═══ */

static bool g_gpiofs_initialized = false;
static uint8_t g_gpio_dir[GPIOFS_PIN_COUNT];  /* 0=INPUT, 1=OUTPUT */

#ifndef NATIVE_TEST
static xeros_spinlock_t g_gpiofs_mux;
#define GPIOFS_ENTER_CRITICAL() xeros_spinlock_lock(&g_gpiofs_mux)
#define GPIOFS_EXIT_CRITICAL()  xeros_spinlock_unlock(&g_gpiofs_mux)
#else
#define GPIOFS_ENTER_CRITICAL() do {} while (0)
#define GPIOFS_EXIT_CRITICAL()  do {} while (0)
#endif

/* ═══ 前向声明 ═══ */

static ssize_t gpiofs_read(kern_file_t *f, char *buf, size_t len);
static ssize_t gpiofs_write(kern_file_t *f, const char *buf, size_t len);

/* ═══ 文件操作表 ═══ */

static kern_file_ops_t g_gpiofs_fops = {
    .read    = gpiofs_read,
    .write   = gpiofs_write,
    .ioctl   = NULL,
    .release = NULL,
};

/* ═══ 引脚索引查询 ═══ */

static int gpio_pin_index(uint8_t pin)
{
    for (size_t i = 0; i < GPIOFS_PIN_COUNT; i++) {
        if (g_gpio_pins[i].pin == pin) {
            return (int)i;
        }
    }
    return -1;
}

/* ═══ 硬件访问辅助 ═══ */

#ifndef NATIVE_TEST

static int gpio_read(uint8_t pin)
{
    GPIOFS_ENTER_CRITICAL();
    int v = gpio_get_level((gpio_num_t)pin);
    GPIOFS_EXIT_CRITICAL();
    return v;
}

static int gpio_get_dir(uint8_t pin)
{
    int idx = gpio_pin_index(pin);
    if (idx < 0) return 0;
    return g_gpio_dir[idx];
}

static void gpio_set_output(uint8_t pin, int value)
{
    GPIOFS_ENTER_CRITICAL();
    int idx = gpio_pin_index(pin);
    if (idx >= 0 && g_gpio_dir[idx] != 1) {
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        g_gpio_dir[idx] = 1;
    }
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);
    GPIOFS_EXIT_CRITICAL();
}

#else /* NATIVE_TEST stubs */

static int gpio_read(uint8_t pin)  { (void)pin; return 0; }
static int gpio_get_dir(uint8_t pin) { (void)pin; return 0; }
static void gpio_set_output(uint8_t pin, int value) { (void)pin; (void)value; }

#endif

/* ═══ 内容生成 ═══ */

/**
 * @brief 生成 /sys/gpio/list 汇总表
 * @return 生成的内容长度；若 max_len 不足则返回 max_len（截断）
 */
static size_t gpiofs_list_generate(char *content, size_t max_len)
{
    size_t pos = 0;

    int written = snprintf(content + pos, max_len - pos,
                           "PIN  DIR   VAL  FUNC\n"
                           "---  ----  ---  ----\n");
    if (written < 0 || (size_t)written >= max_len - pos) return pos;
    pos += (size_t)written;

    for (size_t i = 0; i < GPIOFS_PIN_COUNT && pos < max_len; i++) {
        uint8_t  pin = g_gpio_pins[i].pin;
        int      val = gpio_read(pin);
        int      dir = gpio_get_dir(pin);
        const char *dir_str = (dir == 0) ? "IN " : "OUT";

        written = snprintf(content + pos, max_len - pos,
                           "%-3u  %s   %d    %s\n",
                           pin, dir_str, val, g_gpio_pins[i].func);
        if (written < 0 || (size_t)written >= max_len - pos) break;
        pos += (size_t)written;
    }
    return pos;
}

/**
 * @brief 生成单个引脚信息
 */
static size_t gpiofs_pin_generate(uint8_t pin, char *content, size_t max_len)
{
    int      val = gpio_read(pin);
    int      dir = gpio_get_dir(pin);
    const char *dir_str = (dir == 0) ? "in" : "out";

    /* 查找功能描述 */
    const char *func = "unknown";
    int idx = gpio_pin_index(pin);
    if (idx >= 0) {
        func = g_gpio_pins[idx].func;
    }

    int written = snprintf(content, max_len,
                           "pin=%u direction=%s value=%d function=\"%s\"\n",
                           pin, dir_str, val, func);
    return (written > 0) ? (size_t)written : 0;
}

/* ═══ 文件操作实现 ═══ */

static ssize_t gpiofs_read(kern_file_t *f, char *buf, size_t len)
{
    if (f == NULL || buf == NULL || len == 0) return KERN_EINVAL;

    char content[GPIOFS_CONTENT_MAX];
    size_t content_len = 0;
    bool truncated = false;

    /* private_data: 对于 list 文件为 -1，对于引脚文件为 GPIO 编号 */
    intptr_t pin_id = (intptr_t)f->inode->private_data;

    if (pin_id == -1) {
        content_len = gpiofs_list_generate(content, sizeof(content));
        if (content_len >= sizeof(content)) {
            truncated = true;
            content_len = sizeof(content) - 1;
        }
    } else if (pin_id >= 0 && pin_id < GPIOFS_PIN_MAX) {
        content_len = gpiofs_pin_generate((uint8_t)pin_id, content, sizeof(content));
    } else {
        return KERN_EINVAL;
    }

    /* EOF */
    if (f->f_pos >= content_len) return 0;

    size_t available = content_len - f->f_pos;
    size_t to_copy = (available < len) ? available : len;
    memcpy(buf, content + f->f_pos, to_copy);
    f->f_pos += to_copy;

    if (truncated && f->f_pos >= content_len) {
        return KERN_ENOSPC;
    }
    return (ssize_t)to_copy;
}

static ssize_t gpiofs_write(kern_file_t *f, const char *buf, size_t len)
{
    if (f == NULL || buf == NULL || len == 0) return KERN_EINVAL;

    intptr_t pin_id = (intptr_t)f->inode->private_data;

    /* /sys/gpio/list 只读 */
    if (pin_id == -1) return KERN_EACCES;

    uint8_t pin = (uint8_t)pin_id;

    /* 检查是否为输出引脚 */
    bool can_output = false;
    int idx = gpio_pin_index(pin);
    if (idx >= 0) {
        can_output = g_gpio_pins[idx].can_output;
    }

    if (!can_output) {
        return KERN_EACCES;  /* 只读引脚 */
    }

    /* 解析写入值：接受 "0" 或 "1" */
    int value = 0;
    if (len > 0 && (buf[0] == '1' || buf[0] == '0')) {
        value = (buf[0] == '1') ? 1 : 0;
    } else {
        return KERN_EINVAL;
    }

    gpio_set_output(pin, value);
    return (ssize_t)len;
}

/* ═══ 初始化 ═══ */

static kern_err_t gpiofs_register_file(const char *name, intptr_t data_id)
{
    char path[KERN_PATH_MAX];
    int written = snprintf(path, sizeof(path), "/sys/gpio/%s", name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return KERN_ENOSPC;
    }

    kern_inode_t *inode = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
    if (inode == NULL) {
        return KERN_ENOMEM;
    }

    inode->type         = KERN_FILE_REGULAR;
    inode->fops         = &g_gpiofs_fops;
    inode->private_data = (void *)data_id;

    kern_err_t rc = kern_dentry_register(path, inode);
    if (rc != KERN_OK) {
        free(inode);
        kern_log(KERN_LOG_WARN, "gpiofs: failed to register %s", path);
    }
    return rc;
}

static void gpiofs_cleanup(void)
{
    kern_vfs_unlink("/sys/gpio/list");
    for (size_t i = 0; i < GPIOFS_PIN_COUNT; i++) {
        char path[KERN_PATH_MAX];
        snprintf(path, sizeof(path), "/sys/gpio/%u", g_gpio_pins[i].pin);
        kern_vfs_unlink(path);
    }
}

kern_err_t kern_gpiofs_init(void)
{
    if (g_gpiofs_initialized) return KERN_OK;

    kern_vfs_init();

    /* 创建 /sys/gpio 目录 */
    kern_vfs_mkdir("/sys/gpio");

    /* 初始化 GPIOFS 自旋锁 */
#ifndef NATIVE_TEST
    xeros_spinlock_init(&g_gpiofs_mux);
#endif

    /* 注册汇总文件 */
    kern_err_t rc = gpiofs_register_file("list", -1);
    if (rc != KERN_OK) {
        return rc;
    }

    /* 注册每个引脚文件 */
    for (size_t i = 0; i < GPIOFS_PIN_COUNT; i++) {
        char pin_name[8];
        snprintf(pin_name, sizeof(pin_name), "%u", g_gpio_pins[i].pin);
        rc = gpiofs_register_file(pin_name, (intptr_t)g_gpio_pins[i].pin);
        if (rc != KERN_OK) {
            gpiofs_cleanup();
            return rc;
        }
    }

    g_gpiofs_initialized = true;
    kern_log(KERN_LOG_INFO, "gpiofs initialized at /sys/gpio");
    return KERN_OK;
}
