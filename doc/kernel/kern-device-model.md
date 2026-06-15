# 统一设备驱动模型

> **Parent:** [内核总览](index.md) | **Related:** [VFS 核心](kern-vfs.md), [设备文件系统](kern-devfs.md), [物理设备](kern-devices.md)

## 概述

v2 引入了**统一设备驱动模型**（`kern_device_t`），为所有硬件驱动提供标准化的操作接口和注册机制。核心思想是将设备驱动的操作签名从 VFS 的 `kern_file_ops_t` 解耦出来，形成独立的 `kern_device_ops_t`，再通过**VFS 桥接层**将两者连接。

![VFS Bridge](assets/diagrams/vfs-bridge.png)

### VFS 与设备桥接流程

```mermaid
sequenceDiagram
    autonumber
    participant App as 应用层<br/>(Shell / 用户任务)
    participant VFS as VFS 层<br/>kern_vfs
    participant Bridge as Bridge fops<br/>g_device_bridge_fops
    participant Dev as 设备驱动模型<br/>kern_device_ops_t
    participant HW as 硬件驱动<br/>(pwrkey / fb0 等)

    App->>VFS: kern_open("/dev/pwrkey", O_RDONLY)
    VFS->>VFS: kern_path_resolve() · 解析路径
    VFS->>VFS: inode 绑定 Bridge fops
    VFS->>VFS: fd_table[] 分配 FD
    VFS-->>App: 返回 fd=3

    App->>VFS: kern_read(fd=3, buf, len)
    VFS->>Bridge: file->fops->read(file, buf, len)
    Bridge->>Bridge: dev = (kern_device_t*)file->inode->private_data
    Bridge->>Dev: dev->ops->read(dev, buf, len, &offset)
    Dev->>HW: 驱动私有 read 实现
    HW-->>Dev: 返回数据
    Dev-->>Bridge: KERN_OK
    Bridge-->>VFS: 返回字节数
    VFS-->>App: 返回实际读取长度

    App->>VFS: kern_close(fd=3)
    VFS->>Bridge: file->fops->release(file)
    Bridge->>Dev: dev->ops->close(dev)
    VFS->>VFS: fd_table[] 释放 FD
    VFS-->>App: KERN_OK
```

---

## 关键概念

### 设备描述符：kern_device_t

*📄 Source: [kern_device.h](../../src/kernel/kern_device.h#L50-L56)*

```c
struct kern_device {
    char                   name[KERN_NAME_MAX + 1];  /* 设备名称（如 "pwrkey"） */
    kern_device_type_t     type;                      /* KERN_DEV_CHAR 或 KERN_DEV_BLOCK */
    kern_device_ops_t     *ops;                       /* 操作函数表 */
    void                  *private_data;              /* 驱动私有数据（如 DMA 缓冲区） */
    struct kern_device    *next;                      /* 全局链表下一节点 */
};
```

每个物理或虚拟设备创建一个 `kern_device_t` 实例。`name` 用于在全局链表中的查找，`ops` 指向设备自己的操作实现，`private_data` 可存储任意驱动特定状态（如缓冲区指针、硬件寄存器基址），`next` 构成全局单链表。

### 设备操作表：kern_device_ops_t

*📄 Source: [kern_device.h](../../src/kernel/kern_device.h#L36-L42)*

```c
typedef struct kern_device_ops {
    kern_err_t  (*open)(kern_device_t *dev, int flags);
    kern_err_t  (*close)(kern_device_t *dev);
    kern_err_t  (*read)(kern_device_t *dev, void *buf, size_t len, size_t *offset);
    kern_err_t  (*write)(kern_device_t *dev, const void *buf, size_t len, size_t *offset);
    kern_err_t  (*ioctl)(kern_device_t *dev, unsigned int cmd, unsigned long arg);
} kern_device_ops_t;
```

**与 `kern_file_ops_t` 的关键差异**：

| 维度 | `kern_file_ops_t`（VFS 签名） | `kern_device_ops_t`（设备签名） |
|------|------------------------------|-------------------------------|
| 第一参数 | `kern_file_t *f` | `kern_device_t *dev` |
| 关闭回调名 | `release` | `close` |
| read/write 偏移量 | 通过 `f->f_pos` 隐式传递 | 通过 `size_t *offset` 显式传递 |
| 返回类型 | `int` / `ssize_t` | `kern_err_t` |
| 关注点 | VFS 文件系统语义 | 设备硬件操作语义 |

这种解耦使设备驱动无需了解 VFS 内部结构（如 `kern_file_t` 的布局），只需关注设备本身。

---

## 全局设备链表（注册表）

### 数据结构

*📄 Source: [kern_device.c](../../src/kernel/kern_device.c#L20)*

```c
static kern_device_t *g_device_list = NULL;   /* 单链表头指针 */
```

所有通过 `kern_device_register()` 注册的设备被加入此链表。链表采用**头插法**（新设备插入链表头部），O(1) 插入，O(n) 查找。

### 注册与查找

*📄 Source: [kern_device.c](../../src/kernel/kern_device.c#L24-L94)*

```c
kern_err_t kern_device_register(kern_device_t *dev)
{
    // 校验：dev 非空，名称非空
    // 去重：若同名设备已存在 → KERN_EEXIST
    // 头插：dev->next = g_device_list; g_device_list = dev;
    // 日志：kern_log("device registered: %s", dev->name)
}

kern_device_t *kern_device_find(const char *name)
{
    // 线性遍历 g_device_list
    // 按名称 strcmp 匹配
    // 找到返回指针，未找到返回 NULL
}
```

#### 中文伪代码拆解

```
函数 注册设备(设备描述符) {
    if (设备名为空) return KERN_EINVAL
    if (同名的设备已在链表中) return KERN_EEXIST  // 防止重复注册

    设备描述符.下一个 = 全局链表头       // 头插法
    全局链表头 = 设备描述符

    记录日志("设备已注册: 设备名")
    return KERN_OK
}

函数 查找设备(设备名) {
    当前 = 全局链表头
    while (当前 != NULL) {
        if (字符串匹配(当前.名称, 设备名)) return 当前
        当前 = 当前.下一个
    }
    return NULL  // 未找到
}
```

### 注册流程图

```
kern_devfs_register_device(&g_pwrkey_dev)
  │
  ├─[1] kern_device_register(&g_pwrkey_dev)
  │       │
  │       ├─ 检查名称非空 ✓
  │       ├─ 检查无同名设备 ✓
  │       ├─ 头插入 g_device_list:
  │       │   g_pwrkey_dev.next = g_device_list
  │       │   g_device_list = &g_pwrkey_dev
  │       └─ 日志: "device registered: pwrkey"
  │
  ├─[2] 构建路径 "/dev/pwrkey"
  │
  ├─[3] kern_device_create_fops(&g_pwrkey_dev)
  │       └─ 返回 &g_device_bridge_fops（共享单例）
  │
  ├─[4] 分配 inode:
  │       inode->type = KERN_FILE_CHRDEV
  │       inode->fops = &g_device_bridge_fops
  │       inode->private_data = &g_pwrkey_dev  ← ★桥接关键
  │
  └─[5] kern_dentry_register("/dev/pwrkey", inode)
          └─ 挂入 VFS 树
```

---

## VFS 桥接层（Bridge）

### 桥接原理

`kern_device_ops_t` 和 `kern_file_ops_t` 签名不同，需要翻译层。Xeros 使用一个**全局共享的单例 bridge fops**（`g_device_bridge_fops`）完成翻译。

所有使用新设备模型的设备共用此 fops，路由通过 `inode->private_data` 完成：

```
任何调用者 → bridge_open(f, flags)
               │
               ├─ file_to_dev(f)  →  从 f->inode->private_data 取 kern_device_t*
               │
               └─ dev->ops->open(dev, flags)  →  调用设备自己的实现
```

### file_to_dev() —— 提取设备指针

*📄 Source: [kern_device.c](../../src/kernel/kern_device.c#L107-L113)*

```c
static kern_device_t *file_to_dev(kern_file_t *f)
{
    if (f == NULL || f->inode == NULL) return NULL;
    return (kern_device_t *)f->inode->private_data;
}
```

这是桥接层的核心：从 `kern_file_t` 一路追溯到 `inode->private_data`，提取出设备指针。此函数在每一个 bridge 函数中被首先调用。

### 五个 bridge 函数

*📄 Source: [kern_device.c](../../src/kernel/kern_device.c#L115-L158)*

| Bridge 函数 | 翻译逻辑 | 空指针安全 |
|------------|---------|-----------|
| `bridge_open(f, flags)` | `dev->ops->open(dev, flags)` | ops 或 open 为 NULL → 返回 KERN_OK |
| `bridge_read(f, buf, len)` | `dev->ops->read(dev, buf, len, &f->f_pos)` | ops 或 read 为 NULL → 返回 KERN_EINVAL |
| `bridge_write(f, buf, len)` | `dev->ops->write(dev, buf, len, &f->f_pos)` | ops 或 write 为 NULL → 返回 KERN_EINVAL |
| `bridge_ioctl(f, cmd, arg)` | `dev->ops->ioctl(dev, cmd, arg)` | ops 或 ioctl 为 NULL → 返回 KERN_ENOTTY |
| `bridge_release(f)` | `dev->ops->close(dev)` | ops 或 close 为 NULL → 返回 KERN_OK |

**关键细节**：`bridge_read` 和 `bridge_write` 将 `f->f_pos` 的地址传给设备的 `read/write`，设备可在内部更新偏移量。`bridge_open` 和 `bridge_release` 对缺失的回调是宽容的（返回 OK），因为很多简单设备不需要打开/关闭逻辑。

### 共享 bridge fops 单例

*📄 Source: [kern_device.c](../../src/kernel/kern_device.c#L167-L179)*

```c
static kern_file_ops_t g_device_bridge_fops = {
    .open    = bridge_open,
    .read    = bridge_read,
    .write   = bridge_write,
    .ioctl   = bridge_ioctl,
    .release = bridge_release,
};

kern_file_ops_t *kern_device_create_fops(kern_device_t *dev)
{
    (void)dev;
    return &g_device_bridge_fops;   /* 所有设备返回同一个实例 */
}
```

**单例设计的意义**：
- 内存效率：无论注册多少个设备，只需一份 bridge 函数表
- 路由解耦：函数表本身不绑定设备，设备指针通过运行时 `inode->private_data` 获取
- 即插即用：新设备只需实现 `kern_device_ops_t`，调用 `kern_devfs_register_device()` 即完成 VFS 集成

---

## 迁移示例：dev_pwrkey.c

`/dev/pwrkey`（电源键设备）是首个从旧版 API 迁移到统一设备模型的设备，作为概念验证。

*📄 Source: [dev_pwrkey.c](../../src/kernel/devices/dev_pwrkey.c#L74-L90)*

### 旧版方式（假设）

如果使用旧版 `kern_dev_register()`，需要：

```c
// 旧版：直接实现 kern_file_ops_t
static kern_file_ops_t pwrkey_fops = {
    .open  = pwrkey_open,     // 签名: int (*)(kern_file_t*, unsigned int)
    .read  = pwrkey_read,     // 签名: ssize_t (*)(kern_file_t*, char*, size_t)
    .write = pwrkey_write,    //   需要了解 kern_file_t 内部结构
    // ...
};
kern_dev_register("pwrkey", &pwrkey_fops, KERN_FILE_CHRDEV, NULL);
```

### 新版方式（实际实现）

```c
// 新版：实现 kern_device_ops_t（设备语义）
static kern_device_ops_t g_pwrkey_ops = {
    .open  = pwrkey_open,     // 签名: kern_err_t (*)(kern_device_t*, int)
    .close = pwrkey_close,    // 签名: kern_err_t (*)(kern_device_t*)
    .read  = pwrkey_read,     // 签名: kern_err_t (*)(kern_device_t*, void*, size_t, size_t*)
    .write = pwrkey_write,    // 只需关注设备本身，无需了解 VFS
    .ioctl = pwrkey_ioctl,
};

kern_device_t g_pwrkey_dev = {
    .name         = "pwrkey",
    .type         = KERN_DEV_CHAR,
    .ops          = &g_pwrkey_ops,
    .private_data = NULL,
    .next         = NULL,
};

// 在 kern_devices_init() 中一行注册：
kern_devfs_register_device(&g_pwrkey_dev);
```

### 设备操作实现

*📄 Source: [dev_pwrkey.c](../../src/kernel/devices/dev_pwrkey.c#L18-L70)*

```c
static kern_err_t pwrkey_read(kern_device_t *dev, void *buf, size_t len, size_t *offset)
{
    if (len < DEV_PWRKEY_EVENT_SIZE) return KERN_EINVAL;

    dev_pwrkey_event_t ev;
    memset(&ev, 0, sizeof(ev));

    hal_pwr_key_event_t e = hal_power_key_get_event();
    if (e != HAL_PWR_KEY_NONE) {
        ev.event     = (uint8_t)e;
        ev.hold_ms   = hal_power_key_get_hold_duration_ms();
        ev.timestamp = hal_get_ticks();
    }

    memcpy(buf, &ev, DEV_PWRKEY_EVENT_SIZE);
    return DEV_PWRKEY_EVENT_SIZE;
}
```

设备只需关注硬件操作——轮询按键硬件、填充事件结构体、拷贝到用户缓冲区。VFS 的 FD 管理、权限检查、路径解析全部由桥接层和 VFS 透明处理。

---

## 完整调用链示例

以下追踪一次 `kern_read(fd, buf, 9)` 调用 `/dev/pwrkey` 的完整路径：

```
用户任务:
  kern_read(fd, buf, 9)
    │
    ├─ fd_get(fd) → 查当前任务 task->fd_table[fd] 获取 kern_file_t*
    │
    ├─ f->fops->read(f, buf, 9)
    │      │
    │      └─ 此时 f->fops == &g_device_bridge_fops
    │         所以调用 bridge_read(f, buf, 9)
    │            │
    │            ├─ file_to_dev(f)
    │            │    → f->inode->private_data == &g_pwrkey_dev
    │            │    → 返回 &g_pwrkey_dev
    │            │
    │            └─ dev->ops->read(&g_pwrkey_dev, buf, 9, &f->f_pos)
    │                  │
    │                  └─ 此时 dev->ops == &g_pwrkey_ops
    │                     所以调用 pwrkey_read(&g_pwrkey_dev, buf, 9, &f->f_pos)
    │                        │
    │                        ├─ hal_power_key_get_event()   ← 硬件轮询
    │                        ├─ 填充 dev_pwrkey_event_t
    │                        ├─ memcpy(buf, &ev, 9)
    │                        └─ return 9
    │
    └─ return 9  (实际读取字节数)
```

---

## 与其他组件的关系

- **kern_vfs**：定义 `kern_file_ops_t`，桥接层的"另一侧"；新设备通过 bridge 使用 VFS
- **kern_devfs**：`kern_devfs_register_device()` 封装了设备注册 + VFS dentry 创建的完整流程
- **物理设备层**：`kern_devices_init()` 中调用新旧两种 API 注册设备
- **kern_resource**：VFS 层 `kern_open()` 自动追踪 FD，设备驱动无需关心资源管理
- **HAL 层**：设备驱动通过 `hal_*` API 访问硬件（如 `hal_power_key_get_event()`）

---

> **See Also:** [VFS 核心](kern-vfs.md) | [设备文件系统](kern-devfs.md) | [物理设备](kern-devices.md) | [GPIO 桥接](kern-gpiofs.md)
