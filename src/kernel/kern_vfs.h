/**
 * @file   kern_vfs.h
 * @brief  Xeros 虚拟文件系统（VFS）头文件
 * @details 定义 inode、dentry、file 三级结构，提供路径解析、
 *          文件打开/关闭/读写接口。遵循"一切皆文件"哲学。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_VFS_H
#define KERN_VFS_H

#include "kern_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 前向声明 ═══ */

struct kern_file;
struct kern_inode;
struct kern_dentry;

/* ═══ 文件操作接口（Linux file_operations 子集） ═══ */

/**
 * @brief 文件操作函数表
 * @note  每个 inode 可以挂载不同的操作函数，实现多态
 */
typedef struct kern_file_ops {
    int     (*open)(struct kern_file *f, unsigned int flags);
    ssize_t (*read)(struct kern_file *f, char *buf, size_t len);
    ssize_t (*write)(struct kern_file *f, const char *buf, size_t len);
    int     (*ioctl)(struct kern_file *f, unsigned int cmd, unsigned long arg);
    int     (*release)(struct kern_file *f);
} kern_file_ops_t;

/* ═══ 打开标志 ═══ */

#define KERN_O_RDONLY   0x01   /* 只读 */
#define KERN_O_WRONLY   0x02   /* 只写 */
#define KERN_O_RDWR     (KERN_O_RDONLY | KERN_O_WRONLY)

/* ═══ 目录项限制 ═══ */

#define KERN_MAX_DENTRY_CHILDREN 256   /* 每个 dentry 最大子节点数 */

/* ═══ 核心结构体 ═══ */

/**
 * @brief inode —— 文件系统对象元数据
 * @note  每个 inode 绑定一个文件操作表，代表一个"文件"
 */
typedef struct kern_inode {
    kern_file_type_t type;           /* 文件类型 */
    kern_file_ops_t *fops;           /* 文件操作函数表 */
    void *private_data;              /* 设备/文件系统私有数据 */
    uint32_t ref_count;              /* 引用计数（dentry + 打开 FD） */
} kern_inode_t;

/**
 * @brief dentry —— 目录项（路径节点）
 * @note  形成树状目录结构，将名称映射到 inode
 */
typedef struct kern_dentry {
    char name[KERN_NAME_MAX + 1];    /* 节点名称 */
    kern_inode_t *inode;             /* 关联的 inode */
    struct kern_dentry *parent;      /* 父节点 */
    struct kern_dentry *children[KERN_MAX_DENTRY_CHILDREN]; /* 子节点 */
    uint8_t child_count;             /* 当前子节点数量 */
} kern_dentry_t;

/**
 * @brief file —— 打开的文件实例
 * @note  文件描述符表指向此结构体
 */
typedef struct kern_file {
    kern_dentry_t *dentry;           /* 对应的目录项 */
    kern_inode_t *inode;             /* 对应的 inode */
    kern_file_ops_t *fops;           /* 操作函数表 */
    unsigned int flags;              /* 打开标志 */
    size_t f_pos;                    /* 当前读写位置 */
    void *private_data;              /* 实例私有数据 */
    bool in_use;                     /* 是否被占用 */
} kern_file_t;

/* ═══ VFS 生命周期 ═══ */

/**
 * @brief 初始化 VFS 子系统
 * @note  创建根 dentry（"/"），初始化文件描述符表
 */
extern void kern_vfs_init(void);

/**
 * @brief  获取根目录项
 * @return 根目录项指针
 */
extern kern_dentry_t *kern_vfs_get_root(void);

/* ═══ 目录项操作 ═══ */

/**
 * @brief  在指定路径下注册一个文件系统对象
 * @param  path  目标路径（如 "/dev/null"）
 * @param  inode 要挂载的 inode
 * @return KERN_OK 成功，< 0 为错误码
 * @note   自动创建中间目录节点
 */
extern kern_err_t kern_dentry_register(const char *path, kern_inode_t *inode);

/**
 * @brief  创建目录（仅创建 dentry，不挂载 inode）
 * @param  path 目录路径（如 "/dev"）
 * @return KERN_OK 成功，< 0 为错误码
 * @note   自动创建中间目录节点；若已存在则幂等返回 KERN_OK
 */
extern kern_err_t kern_vfs_mkdir(const char *path);

/**
 * @brief  删除文件或空目录
 * @param  path 要删除的路径
 * @return KERN_OK 成功，< 0 为错误码
 * @note   仅从目录树中移除 dentry；非空目录返回 KERN_ENOTEMPTY
 */
extern kern_err_t kern_vfs_unlink(const char *path);

/**
 * @brief  创建空文件
 * @param  path 文件路径（如 "/tmp/notes"）
 * @return KERN_OK 成功，KERN_EEXIST 已存在，< 0 为其他错误
 * @note   自动创建中间目录节点
 */
extern kern_err_t kern_vfs_touch(const char *path);

/**
 * @brief  按路径解析目录项
 * @param  path 绝对路径（以 "/" 开头）
 * @return 目录项指针；未找到时返回 NULL
 */
extern kern_dentry_t *kern_path_resolve(const char *path);

/* ═══ 文件描述符操作 ═══ */

/**
 * @brief  打开文件
 * @param  path  文件路径
 * @param  flags 打开标志（KERN_O_RDONLY / KERN_O_WRONLY / KERN_O_RDWR）
 * @return >= 0 为文件描述符，< 0 为错误码
 */
extern kern_fd_t kern_open(const char *path, unsigned int flags);

/**
 * @brief  关闭文件描述符
 * @param  fd 文件描述符
 * @return KERN_OK 成功，< 0 为错误码
 */
extern kern_err_t kern_close(kern_fd_t fd);

/**
 * @brief  从文件描述符读取数据
 * @param  fd  文件描述符
 * @param  buf 输出缓冲区
 * @param  len 读取长度
 * @return > 0 为实际读取字节数，0 为 EOF，< 0 为错误码
 */
extern ssize_t kern_read(kern_fd_t fd, char *buf, size_t len);

/**
 * @brief  向文件描述符写入数据
 * @param  fd  文件描述符
 * @param  buf 输入缓冲区
 * @param  len 写入长度
 * @return > 0 为实际写入字节数，< 0 为错误码
 */
extern ssize_t kern_write(kern_fd_t fd, const char *buf, size_t len);

/**
 * @brief  对文件描述符执行设备控制操作
 * @param  fd  文件描述符
 * @param  cmd 控制命令
 * @param  arg 可选参数
 * @return KERN_OK 成功，< 0 为错误码
 */
extern kern_err_t kern_ioctl(kern_fd_t fd, unsigned int cmd, unsigned long arg);

#ifdef NATIVE_TEST
/**
 * @brief  获取 inode 当前引用计数（仅用于测试）
 * @param  inode 目标 inode
 * @return 引用计数值；inode 为 NULL 时返回 0
 */
extern uint32_t kern_vfs_inode_ref_count(const kern_inode_t *inode);
#endif /* NATIVE_TEST */

#ifdef __cplusplus
}
#endif

#endif /* KERN_VFS_H */
