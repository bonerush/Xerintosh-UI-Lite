# VFS 虚拟文件系统

> **Parent:** [内核总览](index.md) | **Related:** [设备文件系统](kern-devfs.md), [设备驱动模型](kern-device-model.md), [/proc 与 /sys](kern-procfs-sysfs.md), [类型系统](kern-types.md)

## 概述

VFS（Virtual File System）是 Xeros "一切皆文件"哲学的核心实现层。它提供 **inode → dentry → file** 三级抽象，将所有资源 —— 显示帧缓冲、按键输入、串口、内核参数、GPIO 引脚 —— 统一为文件节点。上层无论是 Shell 命令还是用户任务，都通过 `kern_open` / `kern_read` / `kern_write` / `kern_close` / `kern_ioctl` 这五个系统调用操作一切。

**省略的特性**（有意为之）：无 page cache、无 dentry LRU 淘汰、无块设备层、无 inode 缓存。所有 VFS 对象常驻内存，消除所有磁盘相关的复杂性。

---

## 关键概念

### 三级结构一览

```
用户调用:  fd = kern_open("/dev/fb0", O_WRONLY);
           ↓
文件描述符表 (task->fd_table[])    ← 每任务独立的 kern_file_t 槽位
           ↓
目录树 (kern_dentry_t 树状结构)   ← 将路径 "/dev/fb0" 映射到 inode
           ↓
索引节点 (kern_inode_t)            ← 绑定具体文件操作表 (kern_file_ops_t) 与引用计数
```

### 第 1 层：inode —— 索引节点

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L51-L56)*

```c
typedef struct kern_inode {
    kern_file_type_t type;           /* KERN_FILE_REGULAR / DIR / CHRDEV / FIFO */
    kern_file_ops_t *fops;           /* 指向操作函数表（多态核心） */
    void *private_data;              /* 文件系统/设备私有数据 */
    uint32_t ref_count;              /* 引用计数（dentry + 打开 FD） */
} kern_inode_t;
```

inode 是 VFS 的"灵魂"——它不存储文件内容，只存储 **类型** 和 **操作函数表指针**。这意味着：

- 一个 `/dev/fb0` 的 inode 绑定 `dev_fb0_get_fops()` 返回的函数表 → 写入时实际操控帧缓冲区
- 一个 `/proc/tasks` 的 inode 绑定 `g_procfs_fops` → 读取时动态生成任务列表文本
- 同一个 VFS 框架可以处理键盘、网络、文件等所有 I/O

`private_data` 字段是关键扩展点：
- devfs 使用它存储设备专用数据指针
- procfs 使用它存储文件类型枚举值（`KERN_PROCFS_TASKS` 等）
- gpiofs 使用它存储引脚编号
- **设备驱动模型（v2）** 使用它存储 `kern_device_t*` 指针，完成 VFS → 设备桥接

#### 中文伪代码拆解

```
结构体 索引节点 {
    文件类型            ← 决定节点语义：普通文件 / 目录 / 字符设备 / 管道
    操作函数表指针       ← 核心！指向 {open, read, write, ioctl, release} 实现
    私有数据指针         ← 按需使用: devfs存设备指针, procfs存文件类型枚举, gpiofs存引脚号
}
// 原理：inode 本身无逻辑，所有行为都委派给 fops。这就是面向接口编程。
```

### 第 2 层：dentry —— 目录项（路径树）

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L61-L67)*

```c
typedef struct kern_dentry {
    char name[KERN_NAME_MAX + 1];    /* 节点名称（如 "dev"、"fb0"） */
    kern_inode_t *inode;             /* 关联的 inode（可为 NULL，表示纯目录节点） */
    struct kern_dentry *parent;      /* 父节点（根节点为 NULL） */
    struct kern_dentry *children[16]; /* 子节点指针数组（最多 16 个） */
    uint8_t child_count;             /* 当前子节点数量 */
} kern_dentry_t;
```

dentry 形成树状命名空间。每个路径分量（如 `/dev/fb0` 中的 `dev` 和 `fb0`）对应一个 dentry 节点。目录 dentry 的 `inode` 可以为 `NULL`（纯路径占位，不挂载操作表），叶子节点（设备/文件）的 `inode` 指向具体的操作实现。

**关键权衡**：children 使用**固定大小数组**（16 槽）而非链表。嵌入式场景目录项极少（`/dev` 下 4 项，`/proc` 下 5 项），数组的 O(n) 遍历开销可忽略，且避免了链表节点的额外内存分配。

### 第 3 层：file —— 打开文件实例

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L74-L82)*

```c
typedef struct kern_file {
    kern_dentry_t *dentry;           /* 对应的目录项 */
    kern_inode_t *inode;             /* 对应的 inode */
    kern_file_ops_t *fops;           /* 操作函数表（缓存 from inode） */
    unsigned int flags;              /* 打开标志 KERN_O_RDONLY / KERN_O_WRONLY / KERN_O_RDWR */
    size_t f_pos;                    /* 当前读写位置 */
    void *private_data;              /* 实例级私有数据 */
    bool in_use;                     /* 是否被占用 */
} kern_file_t;
```

每次 `kern_open()` 创建一个 `kern_file_t` 实例并放入**当前任务**的 `task->fd_table[]` 槽位。`f_pos` 由 VFS 层自动维护 —— 每次成功的 read/write 后递增实际传输的字节数。`private_data` 允许设备在单次打开会话中保存临时状态。

### 文件操作函数表（v2 签名）

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L31-L37)*

```c
typedef struct kern_file_ops {
    int     (*open)(struct kern_file *f, unsigned int flags);
    ssize_t (*read)(struct kern_file *f, char *buf, size_t len);
    ssize_t (*write)(struct kern_file *f, const char *buf, size_t len);
    int     (*ioctl)(struct kern_file *f, unsigned int cmd, unsigned long arg);
    int     (*release)(struct kern_file *f);
} kern_file_ops_t;
```

v2 较旧版的关键变化：
1. **新增 `open` 回调**：在 `kern_open()` 中，`fd_alloc()` 之后立即调用 `fops->open(f, flags)`。设备可在此做初始化（如清空缓冲区），返回非零则 `open` 失败。
2. **新增 `release` 回调**：在 `kern_close()` 中调用 `fops->release(f)`，设备可在此做清理。
3. **签名简化**：去掉了旧的 `inode` 参数，所有操作函数第一个参数统一为 `kern_file_t*`（file 已内嵌 inode 指针）。

---

## 核心流程

### open() 完整流程

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L410-L464)*

```
kern_open("/dev/fb0", KERN_O_WRONLY):
  │
  ├─[1] 路径解析: kern_path_resolve("/dev/fb0")
  │     └→ 从根 dendry 出发，逐级查找 "dev" → "fb0" dentry
  │     └→ 返回 fb0 对应的 dentry（其 inode->fops 指向帧缓冲操作表）
  │
  ├─[2] 分配 FD: fd_alloc()
  │     └→ 在当前任务 task->fd_table[] 中找第一个空槽位
  │     └→ 分配 kern_file_t 并设置 in_use = true
  │
  ├─[3] 填充 kern_file_t:
  │     └→ fops = inode->fops（缓存操作表）
  │     └→ f_pos = 0, flags = 传入标志
  │
  ├─[4] 递增 inode 引用计数:
  │     └→ kern_inode_ref(inode)  ← dentry 持有 1 份引用，每个打开 FD 再持 1 份
  │
  ├─[5] 调用设备 open（如果存在）:
  │     └→ f->fops->open(f, flags)
  │     └→ 若返回非零 → 递减 inode 引用、回收 file 结构与 FD 槽位 → 返回错误码
  │
  ├─[6] 追踪 FD 资源（v2 新增）:
  │     └→ kern_resource_track(cur_task, (void *)(intptr_t)(fd + 1), KERN_RES_FD, fd_release)
  │     └→ 任务退出时自动 kern_close(fd)
  │
  └─[7] 返回 fd (0 ~ KERN_MAX_FD_PER_TASK-1)
```

#### 中文伪代码拆解

```
函数 内核打开文件(路径, 标志) {
    目录项 = 路径解析(路径)
    if (目录项 == NULL) return KERN_ENOENT
    if (目录项.索引节点 == NULL) return KERN_EISDIR  // 打开了目录而不是文件

    文件描述符 = 分配文件描述符槽位()  // 在当前任务 task->fd_table[] 中找空位
    if (文件描述符 < 0) return 文件描述符  // 返回 KERN_EMFILE / KERN_ENOMEM / KERN_EBADF

    文件 = task->fd_table[文件描述符]
    文件.索引节点 = 目录项.索引节点
    文件.操作表   = 目录项.索引节点.操作表

    // 打开 FD 对 inode 增加一份引用
    inode引用递增(文件.索引节点)

    // 调用设备初始化回调
    if (文件.操作表.打开 != NULL) {
        结果 = 文件.操作表.打开(文件, 标志)
        if (结果 != KERN_OK) {
            inode引用递减(文件.索引节点)
            回收 file 结构与 FD 槽位
            return 结果
        }
    }

    // 将 FD 登记到当前任务的资源追踪列表（存储 fd+1，避免 fd==0 时 ptr 为 NULL）
    资源追踪(当前任务, (void *)(intptr_t)(文件描述符 + 1), FD类型, fd释放回调)

    return 文件描述符
}
```

### read() / write() 流程

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L486-L516)*

```
kern_read(fd, buf, len):
  [1] fd_get(fd) → 查当前任务 task->fd_table[fd] 获取 kern_file_t*
  [2] 检查 fops->read 非空 → 否则 KERN_EINVAL
  [3] 调用 fops->read(f, buf, len) → 实际传输字节数
  [4] 返回传输字节数（VFS 不管理 f_pos，由设备自行维护）

kern_write(fd, buf, len):
  [1-2] 同上
  [3] 调用 fops->write(f, buf, len)
  [4] 返回传输字节数
```

**注意**：v2 中 `f_pos` 的管理下放到了具体设备/文件系统实现。procfs 使用它做分页读取，devfs 中 bridge 函数将它传给 `kern_device_ops_t::read(dev, buf, len, &f->f_pos)`。

### close() 与资源回收（v2 新增）

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L467-L484)*

```
kern_close(fd):
  [1] fd_get(fd) → 查当前任务 task->fd_table[fd]
  [2] kern_resource_untrack(cur_task, (void *)(intptr_t)(fd + 1)) → 从资源追踪链表移除
  [3] fops->release(f) → 设备清理回调
  [4] 将 task->fd_table[fd] 置 NULL，释放 file 结构
  [5] kern_inode_unref(inode) → 递减 inode 引用计数
  [6] return KERN_OK
```

关闭时自动从资源追踪中注销，并递减 inode 引用计数。这意味着即使任务忘记手动 `kern_close()`，`kern_exit()` 时也会通过资源追踪自动回收所有 FD。

### inode 引用计数

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L33-L62)*

v2 为 `kern_inode_t` 新增 `ref_count` 字段，用于跟踪还有多少个 dentry 和打开 FD 指向该 inode：

```c
typedef struct kern_inode {
    kern_file_type_t type;           /* 文件类型 */
    kern_file_ops_t *fops;           /* 文件操作函数表 */
    void *private_data;              /* 设备/文件系统私有数据 */
    uint32_t ref_count;              /* 引用计数（dentry + 打开 FD） */
} kern_inode_t;
```

生命周期规则：

| 操作 | 引用计数变化 | 说明 |
|------|-------------|------|
| `kern_dentry_register()` 挂载 inode | `ref_count++` | dentry 持有一份引用 |
| `kern_open()` 成功 | `ref_count++` | 打开 FD 持有一份引用 |
| `kern_close()` | `ref_count--` | FD 释放引用 |
| `kern_vfs_unlink()` | `ref_count--` | dentry 释放引用 |
| `ref_count == 0` | 释放 inode | 由 `kern_inode_unref()` 调用 `free(inode)` |

这种设计允许文件仍被打开时被 `unlink()`：dentry 被移除但 inode 不会立即释放，直到最后一个 FD 关闭。`private_data` 由创建者（设备驱动/文件系统）持有，VFS 不负责释放。

#### 测试辅助函数

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L184-L191)*

```c
#ifdef NATIVE_TEST
uint32_t kern_vfs_inode_ref_count(const kern_inode_t *inode);
#endif /* NATIVE_TEST */
```

`kern_vfs_inode_ref_count()` 仅在 Native 测试环境下暴露，用于验证引用计数在 open/close/unlink 路径中的正确性。

---

## 路径解析（Path Walker）

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L40-L106)*

路径解析由内部函数 `path_walk()` 实现，核心算法：

```
path_walk(root, "/dev/fb0", auto_create=false):
  当前节点 = root
  跳过前导 "/"

  遍历路径分量:
    提取下一个分量名 ("dev", "fb0")
    在当前节点的 children[] 中线性查找匹配的子节点
    若找到 → 进入子节点继续
    若未找到:
      auto_create=true  → calloc 新 dentry，插入 children[]
      auto_create=false → return NULL
  返回最终到达的 dentry
```

**关键细节**：`auto_create=true` 仅用于 `kern_dentry_register()`（注册新节点）和 `kern_vfs_mkdir()`（创建目录）。对于 `kern_open()` 等只读操作，使用 `auto_create=false`，不存在的路径直接返回 `KERN_ENOENT`。

---

## 目录项注册 API

*📄 Source: [kern_vfs.h](../../src/kernel/kern_vfs.h#L99-L138)*

| API | 用途 | 关键行为 |
|-----|------|----------|
| `kern_dentry_register(path, inode)` | 将 inode 挂载到路径 | 自动创建中间目录，覆盖已有 inode |
| `kern_vfs_mkdir(path)` | 创建目录 | 幂等：目录已存在时返回 KERN_OK |
| `kern_vfs_touch(path)` | 创建空文件 | 已存在时返回 KERN_EEXIST |
| `kern_vfs_unlink(path)` | 删除文件/空目录 | 非空目录返回 KERN_ENOTEMPTY |
| `kern_path_resolve(path)` | 按路径查找 dentry | 只读查找，不自动创建 |

---

## VFS 与设备驱动模型的桥接（v2）

v2 引入了统一设备驱动模型（`kern_device_t`），它的操作表签名（`kern_device_ops_t`）与 VFS 的 `kern_file_ops_t` 不同。桥接机制通过一个**共享的单例 bridge fops** 完成翻译：

```
VFS 层调用                Bridge 翻译              Device 层响应
─────────────            ────────────             ──────────────
fops->open(f, flags)  → bridge_open()  → dev->ops->open(dev, flags)
fops->read(f, buf, n) → bridge_read()  → dev->ops->read(dev, buf, n, &f->f_pos)
fops->write(f,buf,n)  → bridge_write() → dev->ops->write(dev, buf, n, &f->f_pos)
fops->ioctl(f,cmd,arg)→ bridge_ioctl() → dev->ops->ioctl(dev, cmd, arg)
fops->release(f)      → bridge_release() → dev->ops->close(dev)
```

桥接的核心是 `file_to_dev(f)` 函数——它从 `f->inode->private_data` 提取 `kern_device_t*` 指针。详细信息见 **[统一设备驱动模型](kern-device-model.md)**。

---

## 与其他组件的关系

- **kern_task**：每个任务退出时通过 `kern_resource_release_all()` 自动关闭所有 FD
- **kern_resource**：`kern_open()` 用 `kern_resource_track()` 追踪 FD，`kern_close()` 用 `kern_resource_untrack()` 注销
- **kern_devfs**：`kern_devfs_init()` → `kern_vfs_mkdir("/dev")`，而后通过 `kern_dentry_register()` 挂载设备节点
- **kern_device**（v2）：通过 bridge fops 将 `kern_device_ops_t` 映射为 `kern_file_ops_t`
- **kern_procfs / kern_sysfs / kern_gpiofs**：各自 `init()` 中调用 VFS API 创建目录和注册文件
- **kern_shell**：所有文件操作命令（`cat`、`ls`、`echo`、`cd`）通过 VFS API 实现

---

> **See Also:** [设备文件系统](kern-devfs.md) | [设备驱动模型](kern-device-model.md) | [/proc 与 /sys](kern-procfs-sysfs.md) | [GPIO 桥接](kern-gpiofs.md)
