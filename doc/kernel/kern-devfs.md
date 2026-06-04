# 设备文件系统（DevFS）

> **Parent:** [内核总览](index.md) | **Related:** [VFS 核心](kern-vfs.md), [设备驱动模型](kern-device-model.md), [物理设备](kern-devices.md)

## 概述

DevFS 负责管理 `/dev` 目录下的所有设备节点。它提供两种注册 API：**旧版** `kern_dev_register()` 基于 `kern_file_ops_t` 直接注册，**新版** `kern_devfs_register_device()` 基于 `kern_device_t` 统一设备模型并通过 VFS bridge 桥接。

物理设备实例（fb0、input0、ttyS0、pwrkey）的注册在 `kern_devices_init()` 中完成。

---

## 关键概念

### 初始化

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L22-L36)*

```c
void kern_devfs_init(void)
{
    if (g_devfs_initialized) return;

    kern_vfs_init();          /* 确保 VFS 先初始化 */
    kern_vfs_mkdir("/dev");   /* 创建 /dev 目录 */

    g_devfs_initialized = true;
    kern_log(KERN_LOG_INFO, "devfs initialized");
}
```

初始化仅做两件事：确保 VFS 已启动，创建 `/dev` 目录。设备注册由外部调用者（`kern_devices_init()`）后续完成。

### 旧版 API：kern_dev_register() —— 直接 fops 注册

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L40-L78)*

```c
int kern_dev_register(const char *name, kern_file_ops_t *fops,
                      kern_file_type_t type, void *private_data)
{
    // 构建完整路径 "/dev/<name>"
    char path[KERN_PATH_MAX];
    snprintf(path, sizeof(path), "/dev/%s", name);

    // 分配 inode，直接绑定传入的 fops
    kern_inode_t *inode = calloc(1, sizeof(kern_inode_t));
    inode->type = type;            // 通常为 KERN_FILE_CHRDEV
    inode->fops = fops;            // 设备自己的 kern_file_ops_t 实现
    inode->private_data = private_data;

    // 注册到 VFS 树
    return kern_dentry_register(path, inode);
}
```

此 API 用于尚未迁移到新设备模型的旧设备（fb0、input0、ttyS0）。它要求设备实现完整的 `kern_file_ops_t` 函数表，签名与 VFS 完全一致。

```
注册流程:
  kern_dev_register("fb0", fb0_fops, CHRDEV, NULL)
    → 构建路径 "/dev/fb0"
    → alloc inode { type=CHRDEV, fops=fb0_fops }
    → kern_dentry_register("/dev/fb0", inode)
    → 设备就绪，可通过 kern_open("/dev/fb0") 访问
```

### 新版 API：kern_devfs_register_device() —— 统一设备模型桥接

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L82-L127)*

```c
int kern_devfs_register_device(kern_device_t *dev)
{
    // 1. 注册到全局设备链表（幂等，已注册则跳过）
    kern_device_register(dev);

    // 2. 构建路径 "/dev/<dev->name>"
    char path[KERN_PATH_MAX];
    snprintf(path, sizeof(path), "/dev/%s", dev->name);

    // 3. 获取 bridge fops（所有新设备共享同一实例）
    kern_file_ops_t *bridge_fops = kern_device_create_fops(dev);

    // 4. 分配 inode，private_data 设为设备指针
    kern_inode_t *inode = calloc(1, sizeof(kern_inode_t));
    inode->type         = KERN_FILE_CHRDEV;
    inode->fops         = bridge_fops;     // 共享的 bridge 函数表
    inode->private_data = dev;             // bridge 通过此字段获取设备指针

    // 5. 注册到 VFS
    return kern_dentry_register(path, inode);
}
```

**两步注册**：新版 API 同时做了两件事——①将设备加入全局设备链表（`g_device_list`），②在 `/dev/<name>` 创建 VFS 节点。VFS 节点使用共享 bridge fops，通过 `inode->private_data` 路由到具体设备。

#### 中文伪代码拆解

```
函数 注册设备到DevFS(设备描述符) {
    // 第一步：加入全局设备注册表
    结果 = 注册设备到全局链表(设备描述符)  // 可被 kern_device_find() 查找到
    if (结果 != OK 且 结果 != 已存在) return 结果

    // 第二步：在 VFS 树中创建 /dev/设备名 节点
    路径 = "/dev/" + 设备名
    bridge操作表 = 创建设备VFS桥接(设备描述符)  // 返回全局共享的 g_device_bridge_fops

    索引节点 = 分配内存(索引节点大小)
    索引节点.类型         = 字符设备
    索引节点.操作表       = bridge操作表
    索引节点.私有数据     = 设备描述符  // ★ 桥接核心：bridge函数从此处取 dev 指针

    目录项注册(路径, 索引节点)
    return KERN_OK
}
```

### 新旧 API 对比

| 维度 | 旧版 `kern_dev_register()` | 新版 `kern_devfs_register_device()` |
|------|---------------------------|-------------------------------------|
| 操作表类型 | `kern_file_ops_t`（VFS 签名） | `kern_device_ops_t`（设备签名） |
| VFS 桥接 | 无（直接绑定） | 通过共享 `g_device_bridge_fops` 翻译 |
| 全局设备链表 | 不参与 | 自动加入 `g_device_list` |
| 设备查找 | 仅通过 VFS 路径 | 额外支持 `kern_device_find("name")` |
| 迁移状态 | fb0/input0/ttyS0 仍使用 | pwrkey 已迁移 |

### /dev/null 黑洞设备

*📄 Source: [kern_devfs.c](../../src/kernel/kern_devfs.c#L38-L60)*（注：此设备在 `kern_devices_init()` 中通过旧版 API 注册，属于物理设备集合）

| 操作 | 行为 | 说明 |
|------|------|------|
| `read` | 返回 0 (EOF) | 读取始终为空 |
| `write` | 返回 `len`（吞掉数据） | 接受一切写入，丢弃数据 |
| `ioctl` | `NULL`（不支持） | — |
| `release` | 返回 `KERN_OK` | 无需清理 |

---

## 设备注册流程图

```
内核启动
  │
  ├── kern_vfs_init()          ← 创建根 dentry "/"
  │
  ├── kern_devfs_init()        ← 创建 /dev 目录
  │
  └── kern_devices_init()      ← 物理设备批量注册
        │
        ├── kern_dev_register("fb0", fb0_fops, ...)
        │     └→ kern_dentry_register("/dev/fb0", inode)
        │
        ├── kern_dev_register("input0", input0_fops, ...)
        │     └→ kern_dentry_register("/dev/input0", inode)
        │
        ├── kern_dev_register("ttyS0", ttyS0_fops, ...)
        │     └→ kern_dentry_register("/dev/ttyS0", inode)
        │
        └── kern_devfs_register_device(&g_pwrkey_dev)   ← 新版 API
              ├→ kern_device_register(&g_pwrkey_dev)     → 加入全局链表
              └→ kern_dentry_register("/dev/pwrkey", inode)
                    └→ inode->fops = g_device_bridge_fops  ← 共享桥接
```

---

## 与其他组件的关系

- **kern_vfs**：所有设备注册最终都调用 `kern_dentry_register()` 挂入 VFS 树
- **kern_device**（v2）：`kern_devfs_register_device()` 内部调用 `kern_device_register()` + `kern_device_create_fops()`
- **物理设备层**：fb0/input0/ttyS0 通过旧版 API 注册，pwrkey 通过新版 API 注册
- **kern_procfs/sysfs/gpiofs**：各自独立初始化，与 devfs 平级，均为 VFS 树的子树

---

> **See Also:** [VFS 核心](kern-vfs.md) | [设备驱动模型](kern-device-model.md) | [物理设备](kern-devices.md) | [/proc 与 /sys](kern-procfs-sysfs.md)
