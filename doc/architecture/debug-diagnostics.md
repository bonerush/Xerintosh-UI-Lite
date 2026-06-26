# 调试与诊断设施设计

> **Parent:** [原生内核架构](xeros-native-kernel.md)

## 概述

本文档描述 Xeros 内核的调试和诊断设施设计。由于 PlatformIO 不允许 LLM 直接查看串口内容，调试信息需要通过 Python 工具复位和查看。

## 调试架构

```
┌─────────────────────────────────────────────┐
│              Xeros 内核                      │
│                                             │
│  ┌─────────────┐  ┌─────────────┐          │
│  │ 任务检查器   │  │ 调度追踪     │          │
│  │ /proc/tasks │  │ 环形缓冲区   │          │
│  └──────┬──────┘  └──────┬──────┘          │
│         │                │                  │
│  ┌──────┴──────┐  ┌──────┴──────┐          │
│  │ 内存分析器   │  │ IPC 争用日志 │          │
│  │ per-task    │  │ mutex/sem   │          │
│  └──────┬──────┘  └──────┬──────┘          │
│         │                │                  │
│  ┌──────┴────────────────┴──────┐          │
│  │      kern_debug 框架          │          │
│  │      /dev/ttyS0 输出          │          │
│  └───────────────┬───────────────┘          │
│                  │                          │
└──────────────────┼──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│           Python 调试工具                    │
│                                             │
│  xeros_debug.py                             │
│  - 串口连接 (115200 baud)                   │
│  - 复位设备                                 │
│  - 实时日志查看                             │
│  - 命令发送 (ps, meminfo, trace)            │
│  - 日志保存和分析                           │
└─────────────────────────────────────────────┘
```

## 任务检查器

### /proc/tasks 接口

```c
// 已有实现: kern_procfs.c
// 显示所有任务的状态、优先级、栈使用等信息

void procfs_tasks_write(kern_file_t *file)
{
    kern_task_t *task = kern_task_list_head();
    while (task != NULL) {
        kern_debug_printf(file, "PID=%-3d %-16s %-8s PRI=%-3d STACK=%-5d/%-5d\n",
            task->pid,
            task->name,
            task_state_str(task->state),
            task->priority,
            kern_task_stack_usage(task),
            task->stack_size);
        task = task->next;
    }
}
```

### 扩展信息

```c
// 新增：详细任务信息
typedef struct {
    kern_pid_t    pid;
    char          name[KERN_TASK_NAME_LEN + 1];
    kern_task_state_t state;
    uint8_t       priority;
    uint8_t       original_priority;  // PI: 原始优先级
    uint8_t       cpu_id;
    size_t        stack_size;
    size_t        stack_usage;
    size_t        stack_highwater;
    uint32_t      total_runtime;      // 总运行时间 (ticks)
    uint32_t      context_switches;   // 上下文切换次数
    uint32_t      scheduler_class;    // 调度器类
} kern_task_info_t;
```

## 调度追踪

### 环形缓冲区

```c
typedef struct {
    uint32_t      timestamp;
    kern_pid_t    pid;
    uint8_t       event;          // SCHED_SWITCH, SCHED_WAKE, SCHED_SLEEP
    uint8_t       cpu_id;
    uint8_t       priority;
} sched_trace_entry_t;

#define SCHED_TRACE_SIZE  256

static sched_trace_entry_t sched_trace_buf[SCHED_TRACE_SIZE];
static volatile uint32_t sched_trace_head = 0;
static volatile uint32_t sched_trace_count = 0;
```

### 事件类型

```c
#define SCHED_EVENT_SWITCH     0x01  // 任务切换
#define SCHED_EVENT_WAKE       0x02  // 任务唤醒
#define SCHED_EVENT_SLEEP      0x03  // 任务睡眠
#define SCHED_EVENT_BLOCK      0x04  // 任务阻塞
#define SCHED_EVENT_YIELD      0x05  // 任务让出
#define SCHED_EVENT_PREEMPT    0x06  // 抢占
```

### 追踪记录

```c
void sched_trace_record(uint8_t event, kern_task_t *task)
{
    uint32_t idx = sched_trace_head % SCHED_TRACE_SIZE;
    sched_trace_buf[idx] = (sched_trace_entry_t){
        .timestamp = xthal_get_ccount(),
        .pid = task->pid,
        .event = event,
        .cpu_id = KERN_THIS_CPU,
        .priority = task->priority,
    };
    sched_trace_head++;
    if (sched_trace_count < SCHED_TRACE_SIZE) {
        sched_trace_count++;
    }
}
```

## 内存分析器

### Per-Task 内存统计

```c
typedef struct {
    kern_pid_t    pid;
    char          name[KERN_TASK_NAME_LEN + 1];
    size_t        alloc_count;     // 分配次数
    size_t        free_count;      // 释放次数
    size_t        current_usage;   // 当前使用量
    size_t        peak_usage;      // 峰值使用量
} kern_task_mem_stats_t;
```

### 内存泄漏检测

```c
// 在 kern_kmalloc.c 中追踪每个分配
typedef struct {
    void         *ptr;
    size_t        size;
    kern_task_t  *owner;
    uint32_t      alloc_tick;
} kern_alloc_record_t;

#define MAX_ALLOC_RECORDS  128
static kern_alloc_record_t alloc_records[MAX_ALLOC_RECORDS];
```

### /proc/meminfo 扩展

```c
void procfs_meminfo_write(kern_file_t *file)
{
    // 系统级内存信息
    kern_debug_printf(file, "=== System Memory ===\n");
    kern_debug_printf(file, "Total:     %d bytes\n", heap_total);
    kern_debug_printf(file, "Free:      %d bytes\n", heap_free);
    kern_debug_printf(file, "Min Free:  %d bytes\n", heap_min_free);
    kern_debug_printf(file, "Largest:   %d bytes\n", heap_largest);

    // Per-task 内存信息
    kern_debug_printf(file, "\n=== Per-Task Memory ===\n");
    kern_task_t *task = kern_task_list_head();
    while (task != NULL) {
        kern_task_mem_stats_t stats = get_task_mem_stats(task);
        kern_debug_printf(file, "PID=%-3d %-16s cur=%-6d peak=%-6d allocs=%-4d\n",
            task->pid, task->name,
            stats.current_usage, stats.peak_usage,
            stats.alloc_count);
        task = task->next;
    }
}
```

## IPC 争用日志

### Mutex 争用追踪

```c
typedef struct {
    kern_mutex_t *mutex;
    kern_pid_t    waiter_pid;
    kern_pid_t    holder_pid;
    uint32_t      wait_start_tick;
    uint32_t      wait_end_tick;
    bool          pi_boosted;     // 是否触发了优先级继承
} mutex_contention_record_t;
```

### 争用统计

```c
typedef struct {
    uint32_t      total_waits;
    uint32_t      total_wait_ticks;
    uint32_t      max_wait_ticks;
    uint32_t      pi_boost_count;
} mutex_stats_t;
```

## Shell 命令

### 已有命令

```c
// kern_shell_cmds.c
// ps        - 列出所有任务
// meminfo   - 显示内存信息
// uptime    - 显示运行时间
// version   - 显示内核版本
```

### 新增命令

```c
// trace     - 显示调度追踪
// ipc       - 显示 IPC 争用统计
// stack     - 显示栈使用详情
// debug     - 切换调试级别

static const shell_cmd_t debug_cmds[] = {
    {"trace",   cmd_trace,   "显示调度追踪"},
    {"ipc",     cmd_ipc,     "显示 IPC 争用统计"},
    {"stack",   cmd_stack,   "显示栈使用详情"},
    {"debug",   cmd_debug,   "切换调试级别"},
    {NULL, NULL, NULL}
};
```

## Python 调试工具

### xeros_debug.py 功能

```python
#!/usr/bin/env python3
"""
Xeros 内核调试工具

使用方法:
    python3 xeros_debug.py /dev/ttyUSB0

功能:
    - 连接串口 (115200 baud)
    - 复位设备
    - 实时查看日志
    - 发送命令
    - 保存日志到文件
"""

import serial
import time
import sys
import threading

class XerosDebugger:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.running = False

    def reset(self):
        """复位设备"""
        self.ser.dtr = False
        time.sleep(0.1)
        self.ser.dtr = True
        time.sleep(0.5)
        print("设备已复位")

    def send_command(self, cmd):
        """发送命令到设备"""
        self.ser.write(f"{cmd}\n".encode())
        time.sleep(0.1)

    def read_output(self, timeout=1.0):
        """读取输出"""
        output = []
        start = time.time()
        while time.time() - start < timeout:
            line = self.ser.readline()
            if line:
                output.append(line.decode('utf-8', errors='replace').strip())
        return output

    def monitor(self, log_file=None):
        """实时监控输出"""
        self.running = True
        if log_file:
            f = open(log_file, 'w')

        def read_thread():
            while self.running:
                line = self.ser.readline()
                if line:
                    text = line.decode('utf-8', errors='replace')
                    print(text, end='')
                    if log_file:
                        f.write(text)
                        f.flush()

        t = threading.Thread(target=read_thread, daemon=True)
        t.start()

        try:
            while True:
                cmd = input()
                if cmd == 'quit':
                    break
                self.send_command(cmd)
        except KeyboardInterrupt:
            pass

        self.running = False
        if log_file:
            f.close()

    def collect_debug_info(self):
        """收集完整调试信息"""
        info = {}
        commands = ['ps', 'meminfo', 'uptime', 'trace', 'ipc']
        for cmd in commands:
            self.send_command(cmd)
            time.sleep(0.5)
            info[cmd] = self.read_output(timeout=1.0)
        return info

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法: python3 xeros_debug.py <serial_port>")
        sys.exit(1)

    dbg = XerosDebugger(sys.argv[1])
    dbg.reset()
    time.sleep(2)  # 等待设备启动
    dbg.monitor(log_file='xeros_debug.log')
```

## 调试级别

```c
typedef enum {
    KERN_DEBUG_OFF      = 0,  // 关闭调试
    KERN_DEBUG_ERROR    = 1,  // 仅错误
    KERN_DEBUG_WARN     = 2,  // 警告
    KERN_DEBUG_INFO     = 3,  // 信息
    KERN_DEBUG_DEBUG    = 4,  // 调试
    KERN_DEBUG_TRACE    = 5,  // 追踪
} kern_debug_level_t;

// 运行时切换
void kern_debug_set_level(kern_debug_level_t level);
kern_debug_level_t kern_debug_get_level(void);
```

## 条件编译

```c
// 编译时开关
#ifdef CONFIG_XEROS_DEBUG
    #define KERN_DEBUG_TRACE_ENABLED  1
    #define KERN_DEBUG_MEM_TRACKING   1
    #define KERN_DEBUG_IPC_LOGGING    1
#else
    #define KERN_DEBUG_TRACE_ENABLED  0
    #define KERN_DEBUG_MEM_TRACKING   0
    #define KERN_DEBUG_IPC_LOGGING    0
#endif

// 使用宏控制
#if KERN_DEBUG_TRACE_ENABLED
    #define SCHED_TRACE_RECORD(event, task) sched_trace_record(event, task)
#else
    #define SCHED_TRACE_RECORD(event, task) do {} while (0)
#endif
```

## 验证策略

### 功能测试

1. 启动内核，运行 shell 命令 `ps`，验证任务列表正确
2. 运行 `meminfo`，验证内存统计准确
3. 运行 `trace`，验证调度事件记录
4. 运行 `ipc`，验证 IPC 争用统计

### 性能测试

1. 测量调试代码对系统性能的影响
2. 验证关闭调试时无额外开销
3. 测量串口输出对实时性的影响

---

> **See Also:** [原生内核架构](xeros-native-kernel.md)
