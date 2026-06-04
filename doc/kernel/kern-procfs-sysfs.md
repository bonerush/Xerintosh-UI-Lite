# /proc 与 /sys 虚拟文件系统

> **Parent:** [内核总览](index.md) | **Related:** [VFS 核心](kern-vfs.md), [设备文件系统](kern-devfs.md), [调度器](kern-task.md), [初始化](kern-init.md)

## 概述

Xeros 提供两类只存在于内存中的虚拟文件系统：

- **procfs**（`/proc/`）：以只读文本形式暴露内核运行时状态。内容在读取时**动态生成**，而非持久化存储。
- **sysfs**（`/sys/`）：暴露可配置的系统参数。**可读写**，写入触发硬件回调。类似 Linux `/sys/class/`。

两者均通过 VFS 的 `kern_dentry_register()` 将 inode 挂载到目录树，底层无任何物理存储。

---

## procfs — 内核信息文件系统

*📄 Source: [kern_procfs.c](../../src/kernel/kern_procfs.c), [kern_procfs.h](../../src/kernel/kern_procfs.h)*

### 文件列表

| 路径 | 内容 | 生成函数 | 文件类型标识 |
|------|------|----------|-------------|
| `/proc/tasks` | 任务列表（PID、名称、状态、栈用量） | `procfs_tasks_generate()` | `KERN_PROCFS_TASKS` |
| `/proc/uptime` | 内核运行时间（秒） | `procfs_uptime_generate()` | `KERN_PROCFS_UPTIME` |
| `/proc/version` | 内核版本、开发者、平台、编译时间 | `procfs_version_generate()` | `KERN_PROCFS_VERSION` |
| `/proc/meminfo` | 堆内存统计（总量/空闲/已用/最小空闲） | `procfs_meminfo_generate()` | `KERN_PROCFS_MEMINFO` |
| `/proc/developer` | 开发者与项目信息 | `procfs_developer_generate()` | `KERN_PROCFS_DEVELOPER` |

### 架构：共享 fops + private_data 分派

所有 procfs 文件共享**同一个** `kern_file_ops_t` 实例 (`g_procfs_fops`)，通过 `inode->private_data` 区分文件类型：

```
g_procfs_fops  (所有 /proc 文件共享)
  ├── .read  = procfs_read()
  │     └→ 读取 inode->private_data 获得文件类型枚举
  │     └→ switch(类型):
  │           KERN_PROCFS_TASKS     → procfs_tasks_generate()
  │           KERN_PROCFS_UPTIME    → procfs_uptime_generate()
  │           KERN_PROCFS_VERSION   → procfs_version_generate()
  │           KERN_PROCFS_MEMINFO   → procfs_meminfo_generate()
  │           KERN_PROCFS_DEVELOPER → procfs_developer_generate()
  │
  ├── .write  = procfs_write()
  │     └→ 始终返回 KERN_EACCES（/proc 只读）
  │
  └── .ioctl = NULL, .release = NULL
```

**关键设计**：`private_data` 不存指针而存**整数枚举值**（通过 `(void*)(uintptr_t)` 转换），这在嵌入式场景避免为每个 procfs 文件多分配一个结构体。

### 动态生成与分页读取

*📄 Source: [kern_procfs.c](../../src/kernel/kern_procfs.c#L174-L217)*

procfs 文件读取时先将全部内容生成到 1024 字节栈缓冲区，再按 `f->f_pos` 做分页拷贝：

```
procfs_read(f, buf, len):
  [1] 根据 inode->private_data 确定文件类型
  [2] 调用对应生成函数，将全部文本写入 content[1024]
  [3] if (f->f_pos >= content_len) → return 0 (EOF)
  [4] 从 content[f->f_pos] 开始拷贝 min(remaining, len) 字节到 buf
  [5] f->f_pos += 拷贝字节数
  [6] return 拷贝字节数
```

这支持 Shell 的 `cat` 等逐次读取操作，即使文件内容超过一次 read 的缓冲区大小。

#### 中文伪代码拆解（/proc/tasks 生成）

```
函数 生成任务列表(输出缓冲区, 最大长度) {
    位置 = 0
    当前任务 = 任务链表头部

    while (当前任务 != NULL 且 位置 < 最大长度) {
        格式化输出: "PID 任务名 状态 栈用量/栈大小\n"
        位置 += 已写入字节数
        当前任务 = 下一个任务
    }
    return 位置  // 实际生成的内容长度
}
```

### 注册流程

*📄 Source: [kern_procfs.c](../../src/kernel/kern_procfs.c#L257-L293)*

```
kern_procfs_init():
  kern_vfs_mkdir("/proc")        ← 创建目录
  procfs_register_file("tasks",     KERN_PROCFS_TASKS)     → /proc/tasks
  procfs_register_file("uptime",    KERN_PROCFS_UPTIME)    → /proc/uptime
  procfs_register_file("version",   KERN_PROCFS_VERSION)   → /proc/version
  procfs_register_file("meminfo",   KERN_PROCFS_MEMINFO)   → /proc/meminfo
  procfs_register_file("developer", KERN_PROCFS_DEVELOPER) → /proc/developer
```

每个 `procfs_register_file()` 内部：`calloc` 新的 `kern_inode_t` → 设置 `fops = &g_procfs_fops`、`private_data = (void*)文件类型枚举` → `kern_dentry_register("/proc/名称", inode)`。

---

## sysfs — 系统配置文件系统

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c), [kern_sysfs.h](../../src/kernel/kern_sysfs.h)*

### 文件列表

| 路径 | 范围 | 默认值 | 说明 |
|------|------|--------|------|
| `/sys/brightness` | 0–255 | 255 | 屏幕亮度 |
| `/sys/rotation` | 0–3 | 0 | 屏幕方向 |
| `/sys/anim_speed` | 0–100 | 92 | 动画速度 |
| `/sys/anim_enabled` | 0–1 | 1 | 动画开关 |
| `/sys/kernel/log_level` | 0–3 | 1 | 日志级别 |
| `/sys/mode` | 0–3 | 0 | 运行模式（0=manual/1=auto/2=calibrate/3=estop） |
| `/sys/ctrl` | 0–2 | 0 | 控制算法（0=stop/1=start/2=reset） |

### 属性表设计

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c#L36-L64)*

所有 sysfs 属性通过一个统一的属性定义表管理，避免了每个属性单独写 read/write 函数：

```c
typedef struct {
    kern_sysfs_attr_t id;        // 属性枚举 ID
    const char       *name;      // 文件名（不含 /sys/ 前缀）
    int32_t          *value_ptr; // 指向内部变量的指针
    int32_t           min_val;   // 最小值
    int32_t           max_val;   // 最大值
} kern_sysfs_attr_def_t;

static kern_sysfs_attr_def_t g_sysfs_attrs[] = {
    { KERN_SYSFS_BRIGHTNESS,   "brightness",   &g_sys_brightness,    0,   255 },
    { KERN_SYSFS_ROTATION,     "rotation",     &g_sys_rotation,      0,   3   },
    { KERN_SYSFS_ANIM_SPEED,   "anim_speed",   &g_sys_anim_speed,    0,   100 },
    // ...
};
```

### 读写实现

sysfs 同样使用**共享 fops + private_data 分派**模式。`private_data` 存储属性在 `g_sysfs_attrs[]` 中的索引。

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c#L85-L163)*

**read**：从 `*def->value_ptr` 读取当前值 → `snprintf` 为字符串 → 拷贝到输出缓冲区。

**write**：从输入缓冲区解析整数（`strtol`）→ 范围校验 → 更新 `*def->value_ptr` → 触发**回调链**。

#### 中文伪代码拆解（sysfs_write）

```
函数 系统属性写入(文件, 输入缓冲, 数据长度) {
    属性索引 = 文件.索引节点.私有数据   // 从 private_data 获取属性表索引
    属性定义 = &g_sysfs_attrs[属性索引]  // 查表获取值指针和范围

    输入字符串 = 从输入缓冲区提取（最多15字符）
    新值 = 字符串转整数(输入字符串)
    if (解析失败) return KERN_EINVAL
    if (新值 < 属性定义.最小值 || 新值 > 属性定义.最大值) return KERN_EINVAL

    *属性定义.值指针 = 新值           // 更新内部值
    通知所有绑定回调(属性索引, 新值)   // 触发硬件同步（如设置亮度）
    return 数据长度
}
```

### 回调绑定机制

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c#L206-L223)*

sysfs 支持为每个属性绑定**多个回调**（最多 4 个/属性）。写入属性值时，所有绑定的回调被同步调用：

```c
int kern_sysfs_bind(KERN_SYSFS_BRIGHTNESS, on_brightness_change, user_data);
```

典型用法：App 层 settings 模块绑定 `brightness` 回调 → 当 Shell 执行 `echo 128 > /sys/brightness` 时 → `on_brightness_change` 被调用 → 内部调用 `hal_display_set_brightness()` 驱动硬件。

**避免循环触发**：提供 `kern_sysfs_update()` 函数，仅更新内部值**不触发回调**。当 UI 操作改变了配置，通过此函数单向同步到 sysfs。

### 属性扩展（五步法）

向 sysfs 添加新属性的完整步骤：

| 步骤 | 文件 | 操作 |
|------|------|------|
| 1 | `kern_sysfs.h` | 在 `KERN_SYSFS_ATTR_COUNT` 之前添加枚举条目 |
| 2 | `kern_sysfs.c` | 添加 `static int32_t g_sys_<name> = <default>;` |
| 3 | `kern_sysfs.c` | 在 `g_sysfs_attrs[]` 数组末尾（`KERN_SYSFS_ATTR_COUNT` 之前）添加表项 |
| 4 | `kern_sysfs.c` | （可选）实现公开的 getter/setter |
| 5 | （外部） | 在 App 层 `kern_sysfs_bind()` 注册回调以响应写入 |

遗漏步骤 3 会导致命令编译通过但运行时 `private_data` 指向无效索引。

---

## procfs 与 sysfs 对比

| 维度 | procfs | sysfs |
|------|--------|-------|
| 数据来源 | 运行时动态生成文本 | 内部 `int32_t` 变量 |
| 读写权限 | 只读 | 可读写 |
| 写入行为 | 返回 `KERN_EACCES` | 更新变量 + 触发回调 |
| fops 实例 | 1 个全局共享 | 1 个全局共享 |
| private_data 含义 | 文件类型枚举 | 属性表索引 |
| 典型用途 | 调试、监控 | 运行时配置 |

---

## 与其他组件的关系

- **kern_vfs**：两者通过 `kern_vfs_mkdir` / `kern_dentry_register` 挂载到 VFS 树
- **kern_task**：`/proc/tasks` 遍历调度器任务链表获取信息
- **kern_version**：`/proc/version` 引用 `kern_version.h` 定义的版本字符串
- **Settings（App 层）**：通过 `kern_sysfs_bind()` 监听 sysfs 变更以同步硬件状态
- **Shell**：`cat /proc/tasks`、`echo 128 > /sys/brightness`、`param set` 等命令通过 VFS 操作
- **Scope 监测引擎**：可注册任意 `/proc/` 或 `/sys/` 路径进行周期采样

---

> **See Also:** [VFS 核心](kern-vfs.md) | [设备文件系统](kern-devfs.md) | [调度器](kern-task.md) | [GPIO 桥接](kern-gpiofs.md)
