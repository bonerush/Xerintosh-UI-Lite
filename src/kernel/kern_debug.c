/**
 * @file   kern_debug.c
 * @brief  Xeros 内核调试与诊断框架实现
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_debug.h"
#include "kern_task.h"
#include "kern_sched.h"
#include "kern_smp.h"
#include "kern_kmalloc.h"

#include <string.h>

/* ═══ 调度追踪 ═══ */

static kern_dbg_sched_trace_entry_t *g_trace_buf = NULL;
static size_t g_trace_count = 0;
static size_t g_trace_head = 0;
static size_t g_trace_filled = 0;

void kern_dbg_sched_trace_init(kern_dbg_sched_trace_entry_t *buf, size_t count)
{
    g_trace_buf = buf;
    g_trace_count = count;
    g_trace_head = 0;
    g_trace_filled = 0;
    if (buf != NULL && count > 0) {
        memset(buf, 0, count * sizeof(kern_dbg_sched_trace_entry_t));
    }
}

void kern_dbg_sched_trace_log(kern_dbg_sched_event_t event, kern_pid_t pid)
{
    if (g_trace_buf == NULL || g_trace_count == 0) return;

    kern_dbg_sched_trace_entry_t *entry = &g_trace_buf[g_trace_head];
    entry->timestamp = g_sched_ticks;
    entry->event = event;
    entry->pid = pid;
    entry->cpu = KERN_THIS_CPU;

    g_trace_head = (g_trace_head + 1) % g_trace_count;
    if (g_trace_filled < g_trace_count) {
        g_trace_filled++;
    }
}

void kern_dbg_sched_trace_dump(void (*cb)(const kern_dbg_sched_trace_entry_t *entry, void *ud), void *ud)
{
    if (g_trace_buf == NULL || cb == NULL) return;

    size_t start = (g_trace_filled < g_trace_count) ? 0 : g_trace_head;
    for (size_t i = 0; i < g_trace_filled; i++) {
        size_t idx = (start + i) % g_trace_count;
        cb(&g_trace_buf[idx], ud);
    }
}

/* ═══ 任务检查 ═══ */

bool kern_dbg_task_info(kern_pid_t pid, kern_dbg_task_info_t *out)
{
    if (out == NULL) return false;

    kern_task_t *task = kern_task_get(pid);
    if (task == NULL) return false;

    out->pid = task->pid;
    strncpy(out->name, task->name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->state = task->state;
    out->priority = task->priority;
    out->cpu_id = task->cpu_id;
    out->stack_size = task->stack_size;
    out->wake_time = task->wake_time;

    /* 计算栈使用量 */
    out->stack_used = 0;
    if (task->stack_base != NULL && task->stack_size > 0) {
        /* 从栈底向上扫描非 canary 字节 */
        for (size_t i = 0; i < task->stack_size; i++) {
            if (task->stack_base[i] != 0xAA) {
                out->stack_used = task->stack_size - i;
                break;
            }
        }
    }

    return true;
}

void kern_dbg_task_dump(void (*cb)(const kern_dbg_task_info_t *info, void *ud), void *ud)
{
    if (cb == NULL) return;

    kern_task_t *task = kern_task_list_head();
    while (task != NULL) {
        kern_dbg_task_info_t info;
        if (kern_dbg_task_info(task->pid, &info)) {
            cb(&info, ud);
        }
        task = task->next;
    }
}

/* ═══ 内存画像 ═══ */

void kern_dbg_mem_info(kern_dbg_mem_info_t *out)
{
    if (out == NULL) return;

    kern_kmem_stat_t stat;
    if (kern_kmem_get_stats(&stat)) {
        out->total_heap = stat.total_bytes;
        out->free_heap = stat.free_bytes;
        out->min_free_heap = stat.min_free_bytes;
        out->allocated = stat.allocated_bytes;
    } else {
        out->total_heap = 0;
        out->free_heap = 0;
        out->min_free_heap = 0;
        out->allocated = 0;
    }

    kern_kmem_pressure_level_t level = kern_kmem_pressure_level();
    out->pressure_level = (uint8_t)level;
}

/* ═══ IPC 竞争日志 ═══ */

#define IPC_LOG_SIZE 64

static kern_dbg_ipc_event_t g_ipc_log[IPC_LOG_SIZE];
static size_t g_ipc_log_head = 0;
static size_t g_ipc_log_filled = 0;

void kern_dbg_ipc_log(uint8_t type, kern_pid_t waiter, kern_pid_t holder, uint32_t wait_ms)
{
    kern_dbg_ipc_event_t *entry = &g_ipc_log[g_ipc_log_head];
    entry->timestamp = g_sched_ticks;
    entry->type = type;
    entry->waiter = waiter;
    entry->holder = holder;
    entry->wait_ms = wait_ms;

    g_ipc_log_head = (g_ipc_log_head + 1) % IPC_LOG_SIZE;
    if (g_ipc_log_filled < IPC_LOG_SIZE) {
        g_ipc_log_filled++;
    }
}

void kern_dbg_ipc_dump(void (*cb)(const kern_dbg_ipc_event_t *entry, void *ud), void *ud)
{
    if (cb == NULL) return;

    size_t start = (g_ipc_log_filled < IPC_LOG_SIZE) ? 0 : g_ipc_log_head;
    for (size_t i = 0; i < g_ipc_log_filled; i++) {
        size_t idx = (start + i) % IPC_LOG_SIZE;
        cb(&g_ipc_log[idx], ud);
    }
}

/* ═══ Shell 命令注册 ═══ */

void kern_debug_shell_init(void)
{
    /* 调试 shell 命令在现有 shell 框架中注册（如有）。
     * 当前版本通过 Python 调试工具 (tools/xeros_debug.py) 访问诊断数据。
     * 未来可通过 shell 命令直接查询任务状态、内存画像等。 */
}
