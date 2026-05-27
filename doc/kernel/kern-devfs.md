# 设备文件系统（Kern DevFS）

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [物理设备](kern-devices.md)

## 概述

`kern_devfs` 实现了 `/dev` 目录的设备文件系统。它使用 `kern_vfs_mkdir("/dev")` 创建目录，`kern_vfs_create("/dev/*")` 创建设备节点，然后为每个设备注册相应的 `file_ops` 实现。

物理设备实际在 `kern_devices_init()` 中初始化并注册到 devfs。

---

## 关键概念

### 初始化流程

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L24-L39)*

```c
void kern_devfs_init(void)
{
    kern_init();  /* 幂等保证内核已启动 */
    kern_vfs_mkdir("/dev");
}
```

devfs 初始化非常简洁 —— 仅创建 `/dev` 目录。设备注册在外部完成（见 [物理设备](kern-devices.md)）。

### 设备节点创建

设备通过 `kern_vfs_create("/dev/device_name", S_IFCHR)` 注册为字符设备节点。创建后返回的 inode 需要手动设置 `i_fops` 函数表以指向设备的 `file_ops` 实现。

### 内置虚拟设备：/dev/null

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L44-L85)*

```c
static int null_write(kern_file_t *file, const unsigned char *buf, uint32_t count)
{
    return (int)count;  /* 黑洞设备：接受所有写入但丢弃数据 */
}

static struct kern_file_ops null_fops = {
    .read   = NULL,        /* null 不可读 */
    .write  = null_write,  /* 写入被丢弃，返回成功 */
    .ioctl  = NULL,
    .release = NULL,
};
```

#### 中文伪代码拆解

```
/dev/null 设备：
    read  → 返回 KERN_ENOTTY (不支持)
    write → 接受所有字节，丢弃数据，返回写入的字节数（假装成功）
    ioctl → 不支持

典型用法：
    sys_write(fd, "garbage", 7) → 返回 7 (数据被吞了)
```

### 函数表分发模式

每种设备通过实现 `kern_file_ops_t` 函数表与 VFS 集成。例如：

| 设备 | open | read | write | ioctl |
|------|------|------|-------|-------|
| `/dev/null` | ✅ 通用 | ❌ `KERN_ENOTTY` | ✅ 黑洞写入 | ❌ |
| `/dev/fb0` | ✅ 通用 | ❌ | ✅ 命令协议 | ✅ 帧缓冲尺寸 |
| `/dev/input0` | ✅ 通用 | ✅ 6 字节事件 | ❌ | ✅ |
| `/dev/ttyS0` | ✅ 通用 | ✅ 读取字符 | ✅ 发送字符 | ✅ 设置 TX 回调 |

---

## 与其他组件的关系

- **kern_vfs**：通过 `kern_vfs_mkdir` + `kern_vfs_create` 在 VFS 树中创建目录和节点
- **kern_devices**：物理设备（fb0、input0、ttyS0）在 devfs 中注册并挂入 VFS 树
- **Shell**：`ls /dev` 通过 VFS 遍历显示设备列表

---

> **See Also:** [VFS](kern-vfs.md) | [物理设备](kern-devices.md) | [/proc 与 /sys](kern-procfs-sysfs.md)
