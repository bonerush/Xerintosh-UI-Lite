# VFS 虚拟文件系统（Kern VFS）

> **Parent:** [内核总览](index.md) | **Related:** [类型系统](kern-types.md), [devfs](kern-devfs.md), [procfs/sysfs](kern-procfs-sysfs.md)

## 概述

VFS（Virtual File System）是 Xeros"一切皆文件"哲学的核心。它提供了一个简化版的 inode/dentry/file 三级结构，将所有资源 —— 帧缓冲、按键输入、内核参数 —— 统一抽象为文件。上层任务通过 `sys_open`、`sys_read`、`sys_write`、`sys_close`、`sys_ioctl` 操作一切资源。

**省略的特性**：无 page cache、无 dentry LRU、无块设备层、无 inode 缓存。所有对象常驻内存。

---

## 关键概念

### 核心数据结构

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L12-L100)*

VFS 的三级结构与其他 Unix 一致，但做了嵌入式优化：结构体简化为最小必要字段，用固定长度数组替代链表（realloc 时批量搬迁）。

#### inode（索引节点）

```c
typedef struct kern_inode {
    uint32_t        i_ino;          /* inode 编号（全局唯一，自增） */
    uint32_t        i_mode;         /* S_IFREG / S_IFDIR / S_IFCHR / S_IFFIFO */
    uint32_t        i_size;         /* 普通文件：数据长度；目录：子项数 */
    uint32_t        i_flags;
    uint32_t        i_atime;        /* 最后访问时间（millis） */
    uint32_t        i_mtime;        /* 最后修改时间 */
    uint32_t        i_ctime;        /* 创建时间 */

    union {
        unsigned char  *i_data;    /* 普通文件：动态分配的数据缓冲区 */
        kern_dentry_t  *i_dentries;/* 目录：子项数组 */
        void           *i_dev;     /* 设备文件：设备指针 */
        void           *i_pipe;    /* FIFO：管道指针 */
    };
    uint32_t         i_dentry_count; /* 子 dentry 数量（仅目录） */
    kern_file_ops_t  i_fops;         /* 文件操作函数表 */
    uint16_t         i_refcount;     /* 引用计数 */
    struct kern_inode *i_parent;     /* 父目录 inode */
    int16_t          i_errno;        /* 最后一次错误码 */
} kern_inode_t;
```

#### 中文伪代码拆解

```
结构体 索引节点 {
    编号              自增全局唯一
    模式              文件类型 ← S_IFREG(普通文件)/S_IFDIR(目录)/S_IFCHR(字符设备)/S_IFFIFO(命名管道)
    大小              普通文件=字节数, 目录=子项数

    联合体 数据指针 {
        普通文件 → 数据缓冲区 (malloc分配)
        目录     → dentry数组
        设备文件 → 设备驱动指针
        命名管道 → pipe/mq 指针
    }

    文件操作函数表     {open, read, write, ioctl, release}
    引用计数          为0时可释放
    上次错误码        用于 errno 语义
}
```

#### dentry（目录项）

```c
typedef struct kern_dentry {
    char           d_name[KERN_NAME_MAX + 1];  /* 目录项名称 */
    kern_inode_t  *d_inode;                    /* 指向对应 inode */
    kern_dentry_t *d_parent;                   /* 父 dentry */
} kern_dentry_t;
```

**设计简化**：dentry 直接嵌入在父目录 inode 的 `i_dentries` 数组中，而非独立分配。这样省去了一个全局 dentry 哈希表的开销，代价是 `lookup()` 需要线性遍历数组。

#### file（打开文件描述）

```c
typedef struct kern_file {
    kern_inode_t   *f_inode;     /* 指向底层 inode */
    uint32_t        f_pos;       /* 当前文件偏移量 */
    uint32_t        f_mode;      /* O_RDONLY / O_WRONLY / O_RDWR */
    uint32_t        f_flags;     /* 打开标志 */
    uint16_t        f_refcount;  /* 引用计数（fork 场景保留） */
} kern_file_t;
```

### 文件操作函数表

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L48-L58)*

```c
typedef struct kern_file_ops {
    int  (*open)    (kern_inode_t *inode, kern_file_t *file);
    int  (*read)    (kern_file_t *file, unsigned char *buf, uint32_t count);
    int  (*write)   (kern_file_t *file, const unsigned char *buf, uint32_t count);
    int  (*ioctl)   (kern_file_t *file, uint32_t cmd, void *arg);
    int  (*release) (kern_inode_t *inode, kern_file_t *file);
    uint16_t _pad;  /* 对齐 */
} kern_file_ops_t;
```

这是 VFS 的核心 —— Linux 风格的面向接口编程。每个文件系统类型（devfs、procfs、sysfs）注册自己的 `file_ops` 实现，上层统一通过函数表调用。

#### 中文伪代码拆解

```
每种文件系统实现不同的操作函数：

devfs:
    fb0:  open=通用打开, read=NULL(不支持), write=写帧缓冲协议写入, ioctl=NULL
    input0: open=通用打开, read=读按键事件缓冲(6字节), write=NULL, ioctl=NULL

procfs:
    tasks: open=通用打开, read=生成任务列表文本到临时缓冲区, write=NULL
    uptime: open=通用打开, read=生成运行时间文本, write=NULL

sysfs:
    brightness: open=通用打开, read=读取settings, write=设置settings
```

### VFS 文件类型

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L12-L20)*

```c
typedef enum {
    KERN_FILE_TYPE_REGULAR     = 0,   /* S_IFREG: 普通文件 */
    KERN_FILE_TYPE_DIRECTORY   = 1,   /* S_IFDIR: 目录 */
    KERN_FILE_TYPE_CHAR_DEVICE = 2,   /* S_IFCHR: 字符设备 */
    KERN_FILE_TYPE_FIFO        = 3,   /* S_IFFIFO: 命名管道 */
} kern_file_type_t;
```

### 路径解析（Path Resolver）

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L195-L280)*

```c
kern_inode_t *kern_vfs_resolve(kern_inode_t *cwd, const char *path)
{
    /* 处理绝对路径：从根节点开始 */
    kern_inode_t *current = (path[0] == '/') ? root_inode : cwd;
    if (path[0] == '/') path++;

    /* 逐级解析 */
    while (*path != '\0') {
        const char *next_slash = strchr(path, '/');
        size_t seg_len = next_slash ? (size_t)(next_slash - path) : strlen(path);

        kern_dentry_t *found = kern_vfs_lookup(current, path, seg_len);
        if (found == NULL) return NULL;  /* 路径段不存在 */
        current = found->d_inode;
        path = next_slash ? next_slash + 1 : path + seg_len;
    }
    return current;
}
```

#### 中文伪代码拆解

```
函数 VFS路径解析(当前目录, 路径字符串) {
    确定起始点:
        if (路径以 "/" 开头) 当前 = 根inode, 跳过前导 "/"
        else 当前 = 当前目录

    逐级遍历:
        while (路径还有字符) {
            找到下一个 "/" 的位置
            提取当前段名称 (在两个 "/" 之间的部分)

            调用查找函数(当前目录inode, 段名称)
            if (返回NULL) {
                设置错误码 = KERN_ENOENT
                return NULL  // 路径不存在
            }

            当前 = 找到的dentry指向的inode
            跳过已解析段继续
        }

    返回最终inode
}
```

### 目录查找

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L115-L155)*

```c
kern_dentry_t *kern_vfs_lookup(kern_inode_t *dir, const char *name, size_t name_len)
{
    if (!S_ISDIR(dir->i_mode)) return NULL;

    for (uint32_t i = 0; i < dir->i_dentry_count; i++) {
        kern_dentry_t *de = &dir->i_dentries[i];
        if (strncmp(de->d_name, name, name_len) == 0 &&
            de->d_name[name_len] == '\0') {
            return de;
        }
    }
    return NULL;
}
```

**目录查找是 O(n) 线性遍历，不是哈希表 O(1)**。这是有意的设计权衡 —— 在嵌入式文件系统中，目录项数量很少（/dev 下有 ~4 项，/proc 下有 ~3 项），线性遍历的开销远小于维护哈希表的复杂度。

### 文件系统初始化

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L330-L365)*

```c
void kern_vfs_init(void)
{
    kern_init();  /* 确保内核已初始化（幂等） */

    root_inode = kern_inode_alloc(inode_counter++, KERN_FILE_TYPE_DIRECTORY);
    root_inode->i_dentries = kern_malloc(expected * sizeof(kern_dentry_t));
    root_inode->i_dentry_count = 0;
    root_inode->i_ino = 0;
    // ...
}
```

整个文件系统的根在 `/`，所有子目录（`/dev`、`/proc`、`/sys`、`/tmp`）通过各子模块的 init 函数（`kern_devfs_init()` 等）挂载到根目录下。

### VFS 顶层 API

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L108-L126)*

```c
int  kern_vfs_open(const char *path, int mode);          /* 打开文件，返回 fd */
int  kern_vfs_close(int fd);                              /* 关闭文件 */
int  kern_vfs_read(int fd, unsigned char *buf, uint32_t count);   /* 读 */
int  kern_vfs_write(int fd, const unsigned char *buf, uint32_t cnt); /* 写 */
int  kern_vfs_ioctl(int fd, uint32_t cmd, void *arg);    /* 控制 */
int  kern_vfs_mkdir(const char *path);                    /* 创建目录 */
int  kern_vfs_unlink(const char *path);                   /* 删除文件或空目录 */
int  kern_vfs_create(const char *path, uint32_t mode);   /* 创建普通文件 */
DIR *kern_vfs_opendir(const char *path);                  /* 打开目录流 */
kern_dentry_t *kern_vfs_readdir(DIR *dirp);               /* 读取目录项 */
int  kern_vfs_closedir(DIR *dirp);                        /* 关闭目录流 */
```

---

## 核心流程

### open() 流程

```
用户态:
    fd = sys_open("/dev/fb0", O_WRONLY);
      ↓
系统调用分发器:
    kern_vfs_open("/dev/fb0", O_WRONLY)
      ↓
    [1] 路径解析: kern_vfs_resolve(root, "/dev/fb0")
        ├→ 解析 "/" → root_inode
        ├→ 查找 "dev" dentry
        ├→ 进入 dev_inode
        ├→ 查找 "fb0" inode
        └→ 返回 fb0_inode
      ↓
    [2] 分配 file 结构: kern_file_alloc(inode, mode)
        ├→ 检查已有 fd 数量 (< MAX_FD_PER_TASK)
        ├→ 创建 kern_file_t
        ├→ f_pos=0, f_refcount=1
        └→ 增加 inode->i_refcount
      ↓
    [3] 调用设备 open:
        if (inode->i_fops.open)
            ret = i_fops.open(inode, file)
        if (ret < 0) → 失败 → 清理 file
      ↓
    [4] 分配 fd: 在 task->fd_table[] 中找空槽
        ├→ 最小可用 fd 号
        └→ task->fd_table[fd] = file
      ↓
    [5] 返回 fd (>= 0) 或错误码 (< 0)
```

### read() 与 write() 流程

```
sys_read(fd, buf, count):
    [1] fd < 0 || fd >= MAX_FD_PER_TASK → KERN_EBADF
    [2] file = task->fd_table[fd]
    [3] if (file == NULL) → KERN_EBADF
    [4] if (inode->i_fops.read == NULL) → KERN_ENOTTY
    [5] ret = i_fops.read(file, buf, count)
    [6] if (ret >= 0) file->f_pos += ret
    [7] return ret

sys_write(fd, buf, count):
    [1]-[3] 同上
    [4] if (inode->i_fops.write == NULL) → KERN_ENOTTY
    [5] ret = i_fops.write(file, buf, count)
    [6] if (ret >= 0) file->f_pos += ret
    [7] return ret
```

**偏移量维护**：VFS 层自动管理 `f_pos`。每次成功的 read/write 后，偏移量递增 `ret` 字节。这允许上层不做偏移量追踪，也能顺序读写（如 Shell 的 pipe read）。

### close() 流程

```
sys_close(fd):
    [1] file = task->fd_table[fd]
    [2] if (file == NULL) → KERN_EBADF
    [3] 调用 inode->i_fops.release(inode, file)
    [4] inode->i_refcount--
    [5] 释放 kern_file_t
    [6] task->fd_table[fd] = NULL
    [7] return KERN_OK
```

---

## 与其他组件的关系

- **kern_task**：每个 task 的 `fd_table[8]` 指向 `kern_file_t`，任务退出时自动 `close_all_fds()`
- **kern_devfs**：`kern_devfs_init()` 创建 `/dev` 目录并注册 `file_ops` 实现
- **kern_procfs / kern_sysfs**：创建并填充 `/proc` 和 `/sys` 目录
- **kern_shell**：Shell 命令（如 `cat`、`ls`、`cd`、`pwd`）全部通过 VFS API 实现

---

> **See Also:** [设备文件系统](kern-devfs.md) | [/proc 与 /sys](kern-procfs-sysfs.md) | [Shell](kern-shell.md)
