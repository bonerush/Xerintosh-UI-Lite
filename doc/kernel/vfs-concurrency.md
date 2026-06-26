# VFS Dentry-Tree 并发保护

> **Parent:** [内核知识地图](../index.md)  
> **Related:** [原生内核调试日志](../debug-xeros-native.md)

## 概述

Xeros VFS（虚拟文件系统）使用全局的 dentry 树（`g_root_dentry`）管理所有路径节点，使用 inode 引用计数管理文件生命周期。在 SMP/原生调度器启用后，多个任务可能并发执行 `kern_open`、`kern_vfs_unlink`、`kern_dentry_register` 等操作，因此必须对 dentry 树和 inode 引用计数进行串行化保护。

## 设计决策

### 全局自旋锁 `g_vfs_lock`

采用一把全局自旋锁保护整个 dentry 树和 inode 引用计数。理由：

1. VFS 操作主要是查找/注册/删除路径，临界区短且频率不高。
2. 当前文件系统规模小（设备文件为主），全局锁不会成为瓶颈。
3. 实现简单，避免细粒度锁带来的复杂锁序问题。

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L27)*

```c
static xeros_spinlock_t g_vfs_lock;  /* 保护 dentry 树、inode 引用计数和初始化状态 */
```

### 路径解析函数 `path_walk_locked`

原 `path_walk` 被重命名为 `path_walk_locked`，约定**调用者必须持有 `g_vfs_lock`**。该函数可能调用 `calloc` 创建中间目录节点，因此必须在锁保护下执行。

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L119-L182)*

### inode 引用计数函数

原 `kern_inode_ref` / `kern_inode_unref` 重命名为 `kern_inode_ref_locked` / `kern_inode_unref_locked`，约定调用者必须持有 `g_vfs_lock`。

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L79-L98)*

## 加锁范围

### 受保护的公共 API

| 函数 | 保护内容 |
|------|----------|
| `kern_dentry_register` | 查找/创建 dentry、挂载/替换 inode |
| `kern_path_resolve` | 只读遍历 dentry 树 |
| `kern_vfs_mkdir` | 创建目录节点 |
| `kern_vfs_unlink` | 从父节点移除 dentry、释放内存、inode unref |
| `kern_vfs_touch` | 创建 inode 并挂载到 dentry |
| `kern_open` | 路径解析、inode ref |
| `fd_close_raw` | inode unref |

*📄 Source: [kern_vfs.c](../../src/kernel/kern_vfs.c#L236-L571)*

## 锁顺序

系统中存在两把锁：

- `g_vfs_lock`：保护 dentry 树和 inode 引用计数。
- `g_fd_pool_lock`：保护文件描述符对象池位图。

**锁顺序约定**：

- `kern_open`：先 `g_fd_pool_lock`（通过 `fd_alloc` → `fd_pool_alloc`），再 `g_vfs_lock`。
- `fd_close_raw`：先 `g_vfs_lock`（用于 inode unref），再 `g_fd_pool_lock`（通过 `fd_pool_free`）。

虽然两把锁在不同阶段被获取，但**不会同时被持有**，因此不存在死锁风险。

### 中文伪代码拆解

```
函数 kern_open(路径, 标志) {
    // 第一步：先分配 FD 槽位（只涉及 FD 池锁）
    文件对象 = fd_alloc(&fd)
    if (分配失败) return 错误码

    // 第二步：进入 VFS 临界区，解析路径并引用 inode
    上锁(g_vfs_lock)
    dentry = path_walk_locked(根, 路径, 不自动创建)
    if (dentry 不存在 或 没有 inode) {
        解锁(g_vfs_lock)
        释放 FD 槽位
        return 错误码
    }
    填充文件对象(dentry, inode)
    inode 引用计数++
    解锁(g_vfs_lock)

    // 第三步：调用设备 open 回调（此时不持锁，避免回调中 yield/阻塞导致死锁）
    if (fops->open 存在) {
        rc = fops->open(文件对象, 标志)
        if (rc 失败) {
            上锁(g_vfs_lock); inode 引用计数--; 解锁(g_vfs_lock)
            释放 FD 槽位
            return rc
        }
    }

    // 第四步：将 FD 注册到当前任务的资源追踪
    资源追踪(fd)
    return fd
}
```

核心思想：**设备回调不在 VFS 锁内执行**，防止回调中可能触发的调度操作导致死锁。

## 未解决的问题

当前实现中，`kern_open` 返回后仍保存 `f->dentry` 指针。如果另一个任务随后 `unlink` 该路径，`dentry` 内存会被释放，`f->dentry` 将变为悬空指针。不过目前：

1. 没有文件操作回调访问 `f->dentry`（仅 `kern_vfs.c:484` 设置，无读取）。
2. 设备文件通常在启动时注册，运行期极少 `unlink`。

因此当前风险可控。若未来需要支持运行期频繁 unlink，应引入 dentry 引用计数：open 时增加引用，close 时释放，unlink 时仅在引用计数归零后真正释放内存。

## 验证结果

- `pio run -e native`：SUCCESS
- `pio run -e m5stick-c-native`：SUCCESS
- `pio test -e native`：527 个用例，523 通过，2 个预存失败，1 个跳过（与基线一致）
- 独立验证代理结论：PASS

---

> **See Also:** [原生内核调试日志](../debug-xeros-native.md) | [内核知识地图](../index.md)
