# /proc 与 /sys 虚拟文件系统

> **Parent:** [内核总览](index.md) | **Related:** [VFS](kern-vfs.md), [devfs](kern-devfs.md), [调度器](kern-task.md), [初始化](kern-init.md)

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
| `/proc/tasks` | 任务列表（PIDs） | `proc_read_tasks()` |
| `/proc/uptime` | 内核运行时间（ms） | `proc_read_uptime()` |
| `/proc/version` | 内核版本字符串 | `proc_read_version()` |

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

## sysfs — 系统配置文件系统

*📄 Source: [kern_sysfs.c](../../src/kernel/kern_sysfs.c), [kern_sysfs.h](../../src/kernel/kern_sysfs.h)*

### 文件列表

| 路径 | 类型 | 说明 |
|------|------|------|
| `/sys/brightness` | 读写 | 屏幕亮度（0–255） |
| `/sys/rotation` | 读写 | 屏幕方向（0–3） |
| `/sys/anim_speed` | 读写 | 动画速度（0–100） |
| `/sys/anim_enabled` | 读写 | 动画开关（0/1） |
| `/sys/kernel/log_level` | 读写 | 日志级别（0–3） |

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

## 与其他组件的关系

- **kern_vfs**：procfs 和 sysfs 通过 `kern_vfs_mkdir` / `kern_vfs_create` 在 VFS 树中创建节点
- **kern_task**：`/proc/tasks` 遍历调度器链表获取任务信息
- **kern_init**：`/sys/kernel/log_level` 设置日志级别
- **settings**（App 层）：`/sys/brightness`、`/sys/rotation` 等通过 settings 模块读写持久化配置
- **Shell**：`cat /proc/tasks`、`echo 128 > /sys/brightness` 等命令通过 VFS API

---

> **See Also:** [VFS](kern-vfs.md) | [devfs](kern-devfs.md) | [调度器](kern-task.md) | [Shell](kern-shell.md)
