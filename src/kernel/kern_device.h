/**
 * @file   kern_device.h
 * @brief  Xeros 统一设备驱动模型头文件
 * @details 定义 kern_device_t / kern_device_ops_t 框架，
 *          提供设备注册表（register/find）和 VFS 桥接辅助函数。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_DEVICE_H
#define KERN_DEVICE_H

#include "kern_types.h"
#include "kern_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 设备类型 ═══ */

typedef enum {
    KERN_DEV_CHAR  = 0,  /* 字符设备 */
    KERN_DEV_BLOCK = 1,  /* 块设备（预留） */
} kern_device_type_t;

/* ═══ 设备操作接口 ═══ */

/* 前向声明：kern_device_ops_t 引用它 */
typedef struct kern_device kern_device_t;

/**
 * @brief 设备操作函数表
 * @note  每个设备实现自己的操作表，kernel 通过 bridge 桥接到 VFS
 *
 * read/write 返回规范：
 * - 成功时返回实际传输字节数（read 返回 0 表示 EOF）
 * - 失败时返回负的错误码（如 KERN_EINVAL）
 * - bridge 层直接透传该值给 VFS
 */
typedef struct kern_device_ops {
    kern_err_t  (*open)(kern_device_t *dev, int flags);
    kern_err_t  (*close)(kern_device_t *dev);
    kern_err_t  (*read)(kern_device_t *dev, void *buf, size_t len, size_t *offset);
    kern_err_t  (*write)(kern_device_t *dev, const void *buf, size_t len, size_t *offset);
    kern_err_t  (*ioctl)(kern_device_t *dev, unsigned int cmd, unsigned long arg);
} kern_device_ops_t;

/* ═══ 设备结构体 ═══ */

/**
 * @brief 设备描述符
 * @note  全局设备链表节点，通过 kern_device_register() 添加到注册表
 */
struct kern_device {
    char                   name[KERN_NAME_MAX + 1];  /* 设备名称 */
    kern_device_type_t     type;                      /* 设备类型 */
    kern_device_ops_t     *ops;                       /* 操作函数表 */
    void                  *private_data;              /* 驱动私有数据 */
    struct kern_device    *next;                      /* 全局链表下一节点 */
};

/* ═══ 设备注册表 API ═══ */

/**
 * @brief  注册设备到全局设备链表
 * @param  dev 设备描述符（必须静态或堆分配，注册后由链表持有）
 * @return KERN_OK 成功，KERN_EEXIST 同名设备已注册，< 0 为其他错误
 */
extern kern_err_t kern_device_register(kern_device_t *dev);

/**
 * @brief  按名称查找设备
 * @param  name 设备名称
 * @return 设备描述符指针，未找到时返回 NULL
 */
extern kern_device_t *kern_device_find(const char *name);

/**
 * @brief 初始化设备注册表（含互斥锁）
 * @note  必须在首次调用 kern_device_register / kern_device_find 前执行
 */
extern void kern_device_init(void);

/**
 * @brief  从注册表反注册设备，并移除 /dev/<name> 节点
 * @param  dev 已注册设备描述符
 * @return KERN_OK 成功；KERN_EINVAL 参数错误；KERN_ENOENT 未找到
 * @note   仅应在无打开 FD 时调用；用于初始化失败回滚
 */
extern kern_err_t kern_device_unregister(kern_device_t *dev);

/* ═══ VFS 桥接 ═══ */

/**
 * @brief  为设备创建 VFS 文件操作表桥接
 * @param  dev 设备描述符（ops 不可为 NULL）
 * @return kern_file_ops_t 指针（静态分配，跨设备共享）
 * @note   返回的 fops 通过 inode->private_data 获取 kern_device_t* 进行分发。
 *         调用者需在注册 VFS inode 时将 private_data 设为设备指针。
 */
extern kern_file_ops_t *kern_device_create_fops(kern_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* KERN_DEVICE_H */
