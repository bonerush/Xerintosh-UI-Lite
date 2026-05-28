# /proc 与 /sys 虚拟文件系统

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [devfs](kern-devfs.md), [调度器](kern-task.md), [初始化](kern-init.md), [Shell](kern-shell.md)

## 概述

Xeros 内核提供两类虚拟文件系统，均不对应物理存储：

- **procfs**（`/proc/`）：以只读文本形式暴露内核运行时状态信息
- **sysfs**（`/sys/`）：提供对系统配置参数的可读写访问（类似 Linux `/sys/class/`）

---

## procfs — 内核信息文件系统

*📄 Source: [kern_procfs.c](../../src/kernel/kern_procfs.c), [kern_procfs.h](../../src/kernel/kern_procfs.h)*

### 文件列表

| 路径 | 内容 | 实现函数 |
|------|------|----------|
| `/proc/tasks` | 任务列表（PIDsssss） | `proc_read_tasks()` |
| `/proc/uptime` | 内核运行时间（ms） | `proc_read_uptime()` |
| `/proc/version` | 内核版本字符串 | `proc_read_version()` |
| `/proc/meminfo` | 堆内存统计（总量/空闲/已用/最小空闲） | `proc_read_meminfo()` |
| `/proc/developer` | 开发者信息 | `proc_read_developer()` |

### 实现方式：动态生成文本

procfs 文件"读取"时动态生成文本，而不是从持久化数据中读取。所有 proc_read_* 函数接收 `buf` 和 `count`，将格式化的文本写入缓冲区。

*📄 Source: [kern_procfs.c](../../src/kernel/kern_procfs.c#L75-L106)*

```c
static int proc_read_tasks(kern_file_t *file, unsigned char *buf, uint32_t count)
{
    char text[512];
    int offset = 0;
    kern_task_t *task = kern_task_list_head();

    while (task != NULL) {
        offset += snprintf(text + offset, sizeof(text) - offset,
                          "PID:%d 状态:%d 名称:%s\n",
                          task->pid, task->state, task->name);
        task = kern_task_list_next(task);
    }

    /* 按文件偏移量返回数据（支持多次 read） */
    uint32_t copy_len = strlen(text) - file->f_pos;
    if (copy_len > count) copy_len = count;
    memcpy(buf, text + file->f_pos, copy_len);
    return (int)copy_len;
}
```

#### 中文伪代码拆解

```
函数 proc读取任务列表(文件指针, 输出缓冲区, 请求大小) {
    文本缓冲区[512字节]
    偏移量 = 0

    遍历所有任务TCB {
        格式化输出("PID:%d 状态:%d 名称:%s\n", pid, state, name)
        移动到下一个任务
    }

    /* 支持分页读取 */
    根据 file->f_pos 计算可复制范围
    复制到输出缓冲区
    返回实际复制的字节数
}
```

---

## sysfs — 系统配置文件系统

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c), [kern_sysfs.h](../../src/kernel/kern_sysfs.h)*

### 文件列表

| 路径 | 类型 | 范围 | 说明 |
|------|------|------|------|
| `/sys/brightness` | 读写 | 0–255 | 屏幕亮度 |
| `/sys/rotation` | 读写 | 0–3 | 屏幕方向 |
| `/sys/anim_speed` | 读写 | 0–100 | 动画速度 |
| `/sys/anim_enabled` | 读写 | 0–1 | 动画开关 |
| `/sys/kernel/log_level` | 读写 | 0–3 | 日志级别 |
| `/sys/mode` | 读写 | 0–3 | 运行模式（Phase 3 新增） |
| `/sys/ctrl` | 读写 | 0–2 | 控制算法（Phase 3 新增） |

### 新增属性：mode 与 ctrl（Phase 3）

#### `/sys/mode` — 运行模式

*📄 Source: [kern_sysfs.h](../../src/kernel/kern_sysfs.h#L29-L30)*

| 值 | 含义 |
|----|------|
| 0 | manual（手动模式） |
| 1 | auto（自动模式） |
| 2 | calibrate（校准模式） |
| 3 | estop（紧急停止） |

通过 `kern_sysfs_get_mode()` / `kern_sysfs_set_mode(val)` 读写，范围校验在 setter 中完成。

#### `/sys/ctrl` — 控制算法启停

*📄 Source: [kern_sysfs.h](../../src/kernel/kern_sysfs.h#L31-L32)*

| 值 | 含义 |
|----|------|
| 0 | stop（停止） |
| 1 | start（启动） |
| 2 | reset（重置） |

通过 `kern_sysfs_get_ctrl()` / `kern_sysfs_set_ctrl(val)` 读写。

### sysfs 属性扩展模式（五步法）

向 `kern_sysfs` 添加新属性时，必须按以下 5 步操作，分布在 3 个文件中：

| 步骤 | 文件 | 操作 |
|------|------|------|
| Step 1 | `kern_sysfs.h` | 在 `KERN_SYSFS_ATTR_COUNT` 之前添加枚举条目，并添加 getter/setter 声明 |
| Step 2 | `kern_sysfs.c` | 添加 `static int32_t g_sys_<name> = <default>;` |
| Step 3 | `kern_sysfs.c` | 在 `g_sysfs_attrs[]` 中添加 `{ KERN_SYSFS_XXX, "name", &g_sys_xxx, min, max }` |
| Step 4 | `kern_sysfs.c` | 实现 getter/setter 函数（含范围校验） |
| Step 5 | `kern_shell_cmds.c` | 添加使用 `/sys/<name>` 路径的 Shell 命令处理器 |

遗漏任何一个步骤都会导致功能静默失效——命令编译通过但运行时返回 "not available"。

### 双向读/写实现

sysfs 与 procfs 不同之处在于支持 **write** 操作。每个文件同时实现了 read（查询当前值）和 write（设置新值）回调。

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c#L75-L108)*

```c
static int sys_read_brightness(kern_file_t *file, unsigned char *buf, uint32_t count)
{
    char text[16];
    int len = snprintf(text, sizeof(text), "%d\n", settings_get_brightness());
    /* 按文件偏移分页输出 */
    if (file->f_pos >= len) return 0;
    memcpy(buf, text + file->f_pos, min(count, len - file->f_pos));
    return len - file->f_pos;
}

static int sys_write_brightness(kern_file_t *file, const unsigned char *buf, uint32_t cnt)
{
    char tmp[16];
    uint32_t copy = cnt < sizeof(tmp) - 1 ? cnt : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy);
    tmp[copy] = '\0';

    int val = atoi(tmp);
    if (val < 0) val = 0;
    if (val > 255) val = 255;

    settings_set_brightness(val);
    hal_display_set_brightness(val);
    return (int)cnt;  /* 返回接受的字节数 */
}
```

#### 中文伪代码拆解

```
函数 读取亮度文件(文件, 输出缓冲, 请求大小) {
    获取当前亮度值
    格式化为文本 "128\n"
    按 file->f_pos 偏移量分页复制到输出缓冲
    返回复制的字节数
}

函数 写入亮度文件(文件, 输入缓冲, 数据长度) {
    从输入缓冲中提取字符串（最多16字节）
    解析为整数
    边界检查: 0-255
    调用 settings 和 HAL 设置亮度
    return 数据长度  // 返回接受的数据量
}
```

对于 `log_level`，写入值会调用 `kern_set_log_level(val)` 立即生效。

---

## 属性表设计

所有 sysfs 属性通过统一的属性表管理：

```c
typedef struct {
    kern_sysfs_attr_t id;    // 属性枚举 ID
    const char       *name;  // 属性名（不含 /sys/ 前缀）
    int32_t          *value; // 指向内部变量的指针
    int32_t           min;   // 最小值
    int32_t           max;   // 最大值
} kern_sysfs_attr_def_t;

static kern_sysfs_attr_def_t g_sysfs_attrs[KERN_SYSFS_ATTR_COUNT] = {
    { KERN_SYSFS_BRIGHTNESS,   "brightness",   &g_sys_brightness,    0,   255 },
    { KERN_SYSFS_ROTATION,     "rotation",     &g_sys_rotation,      0,   3   },
    // ...
    { KERN_SYSFS_MODE,         "mode",         &g_sys_mode,          0,   3   },
    { KERN_SYSFS_CTRL,         "ctrl",         &g_sys_ctrl,          0,   2   },
};
```

数组大小由 `KERN_SYSFS_ATTR_COUNT` 枚举值自动确定，新增属性只需在枚举末尾之前插入即可。

---

## 与其他组件的关系

- **kern_vfs**：procfs 和 sysfs 通过 `kern_vfs_mkdir` / `kern_vfs_create` 在 VFS 树中创建节点
- **kern_task**：`/proc/tasks` 遍历调度器链表获取任务信息
- **kern_init**：`/sys/kernel/log_level` 设置日志级别
- **settings**（App 层）：`/sys/brightness`、`/sys/rotation` 等通过 settings 模块读写持久化配置
- **Shell**：`cat /proc/tasks`、`echo 128 > /sys/brightness`、`param set`、`mode set` 等命令通过 VFS API 操作
- **Scope**：可注册任意 `/proc/` 或 `/sys/` 路径进行周期监测

---

> **See Also:** [VFS](kern-vfs.md) | [devfs](kern-devfs.md) | [调度器](kern-task.md) | [Shell](kern-shell.md)
