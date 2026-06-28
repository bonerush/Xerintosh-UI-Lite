/**
 * @file   kern_task_lifecycle.c
 * @brief  Xeros 任务生命周期管理
 * @details 任务创建（spawn）、销毁（exit/kill）、让出（yield）、
 *          休眠（sleep）、ZOMBIE 回收、系统任务保护检查。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_task.h"
#include "kern_resource.h"
#include "kern_init.h"
#include "kern_port.h"
#include "kern_sched_rr.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#endif

/* ═══ 任务创建公共辅助函数 ═══ */

static kern_task_t *task_create_common(const char *name,
                                       void (*entry)(void *arg),
                                       void *arg)
{
    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) {
        return NULL;
    }

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->base_priority = 128;
    task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    task->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
    task->cpu_id = KERN_CPU_ANY;
#ifdef CONFIG_SMP_ENABLED
    task->cpu_id = kern_smp_migrate_assign();
#endif
    task->entry = entry;
    task->arg = arg;
    task->notify_state = KERN_NOTIFY_NOT_WAITING;
    task->notify_value = 0;
    task->runtime_us = 0;
    task->last_start_us = 0;
    task->cpu_percent = 0;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    /* 初始化每任务文件描述符表 */
    memset(task->fd_table, 0, sizeof(task->fd_table));

    return task;
}

static void task_list_append(kern_task_t *task)
{
    if (task == NULL) {
        return;
    }

#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif
    if (g_task_list == NULL) {
        g_task_list = task;
        g_task_list_tail = task;
    } else {
        if (g_task_list_tail != NULL) {
            g_task_list_tail->next = task;
        } else {
            /* 尾指针未初始化：回退 O(n) 遍历 */
            kern_task_t *t = g_task_list;
            while (t->next != NULL) t = t->next;
            t->next = task;
        }
        g_task_list_tail = task;
    }
    g_task_count++;
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif
}

static void task_enqueue_to_class(kern_task_t *task)
{
    if (task == NULL
        || task->scheduler_class_id < 0
        || task->scheduler_class_id >= KERN_SCHED_MAX_CLASSES) {
        return;
    }

    kern_sched_class_t *cls = g_sched_classes[task->scheduler_class_id];
    if (cls != NULL && cls->enqueue != NULL) {
        cls->enqueue(task);
    }
}

/* ═══ 任务创建 ═══ */

#ifdef NATIVE_TEST

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_sched_ensure_initialized();

    kern_task_t *task = task_create_common(name, entry, arg);
    if (task == NULL) return KERN_ENOMEM;

    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_STACK_MIN;
    task_stack_init(task, stack_sz);

    if (getcontext(&task->ctx) < 0) {
        kern_log(KERN_LOG_WARN, "getcontext failed for task %s", task->name);
        kern_resource_release_all(task);
        free(task);
        return KERN_ERR;
    }
    task->ctx.uc_stack.ss_sp    = task->stack_base;
    task->ctx.uc_stack.ss_size  = task->stack_size;
    task->ctx.uc_stack.ss_flags = 0;
    task->ctx.uc_link = NULL;

    g_switch_to_task = task;
    makecontext(&task->ctx, task_entry_trampoline, 0);
    task_write_canary(task);

    kern_mpu_setup_stack_guard(task, task->stack_base, task->stack_size);

    task_list_append(task);
    task_enqueue_to_class(task);

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#elif defined(XEROS_NATIVE_SCHED)

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_sched_ensure_initialized();

    kern_task_t *task = task_create_common(name, entry, arg);
    if (task == NULL) return KERN_ENOMEM;

    /* 初始化任务私有栈与原生上下文 */
    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_STACK_MIN;
    task_stack_init(task, stack_sz);
    task->native_ctx = (kern_ctx_native_t *)kern_kmalloc_for_task(
        task, sizeof(kern_ctx_native_t));
    if (task->native_ctx == NULL || task->stack_base == NULL) {
        kern_log(KERN_LOG_WARN, "native: failed to alloc ctx/stack for %s", task->name);
        kern_resource_release_all(task);
        free(task);
        return KERN_ENOMEM;
    }
    memset(task->native_ctx, 0, sizeof(kern_ctx_native_t));
    task->native_ctx_valid = false;
    task_write_canary(task);

    kern_mpu_setup_stack_guard(task, task->stack_base, task->stack_size);

    task_list_append(task);
    task_enqueue_to_class(task);

    /* 若新任务绑定在另一核心，发送 IPI 使其尽快调度 */
    kern_smp_ipi_reschedule(task->cpu_id);

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#endif /* XEROS_NATIVE_SCHED */

/* ═══ Yield ═══ */

#ifdef NATIVE_TEST

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    if (cur->state == KERN_TASK_RUNNING) {
        cur->state = KERN_TASK_READY;
    }
    g_current_task = g_idle_task;

    swapcontext(&cur->ctx, &g_sched_ctx);
}

#else /* XEROS_NATIVE_SCHED */

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    if (cur->state == KERN_TASK_RUNNING) {
        cur->state = KERN_TASK_READY;
    }

    /* 通过可移植层归还令牌并等待下次调度 */
    kern_port_task_yield();

    /* 被唤醒：调度器已把我们设为 RUNNING */
}

#endif

/* ═══ Exit ═══ */

#ifdef NATIVE_TEST

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    /* Native 后端：不能在这里释放栈内存，因为当前正在该栈上运行。
     * 资源回收推迟到 reap_zombies()，在任务已脱离调度链表、不会继续执行后再释放。 */
    cur->stack_base = NULL;

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;
    /* 使用 swapcontext 切换回调度器上下文，避免 macOS setcontext 对
     * 未初始化/无效 uc_stack 的严格检查导致 SIGTRAP。 */
    swapcontext(&cur->ctx, &g_sched_ctx);
    /* 不应到达此处 */
}

#else /* ESP32: exit = 标记 ZOMBIE，通过可移植层销毁自身 */

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    kern_resource_release_all(cur);
    cur->stack_base = NULL;

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;

    /* 通过可移植层归还令牌并销毁当前线程（不会返回） */
    kern_port_task_exit();
}

#endif

/* ═══ Sleep ═══ */

#ifdef NATIVE_TEST

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;
    g_current_task = g_idle_task;

    swapcontext(&cur->ctx, &g_sched_ctx);
}

#else /* XEROS_NATIVE_SCHED: sleep = yield + 记录唤醒时间 */

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;

    /* 直接让出 CPU；若调用 kern_yield() 会把刚刚设置的 SLEEPING 改回 READY */
    kern_port_task_yield();
}

#endif

/* ═══ 系统任务保护检查 ═══ */

bool kern_task_is_protected(const kern_task_t *task)
{
    if (task == NULL) return false;

    /* 虚任务自身不是系统关键任务，但 taskmgr 自身受保护 */
    static const char *protected_names[] = {
        "idle", "shell", "ui",
        "taskmgr", "Task Manager",  /* UI App 英文名保护 */
        NULL
    };

    for (int i = 0; protected_names[i] != NULL; i++) {
        if (strcmp(task->name, protected_names[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* ═══ 外部任务终止 ═══ */

kern_err_t kern_task_kill(kern_pid_t pid)
{
    kern_task_t *task = kern_task_get(pid);
    if (task == NULL) return KERN_ENOENT;

    /* 不能终止受保护的系统任务 */
    if (kern_task_is_protected(task)) return KERN_EACCES;

    /* 不能终止自身（调用 kern_exit() 代替） */
    if (task == g_current_task) return KERN_EACCES;

    /* 已经是 ZOMBIE 则幂等返回成功 */
    if (task->state == KERN_TASK_ZOMBIE) return KERN_OK;

    /* 虚任务：走专门的注销路径（回收 TCB，count--） */
    if (task->flags & KERN_TASK_FLAG_VIRTUAL) {
        kern_task_unregister_virtual(pid);
        return KERN_OK;
    }

    /* 非虚任务：先释放追踪资源，再标记 ZOMBIE */
    kern_resource_release_all(task);
    task->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) killed", task->pid, task->name);

    /* TCB 在下次 sched_tick 时由 reap_zombies 回收；栈与上下文作为资源已追踪 */

    return KERN_OK;
}

/* ═══ ZOMBIE 回收 ═══ */

void reap_zombies(void)
{
#ifdef CONFIG_SMP_ENABLED
    /* 自旋锁保护：避免两个核心同时修改共享任务链表 */
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif

    /* 遍历所有已注册调度类的任务链表，回收 ZOMBIE 任务 */
    for (uint8_t c = 0; c < g_sched_class_count; c++) {
        kern_sched_class_t *cls = g_sched_classes[c];
        if (cls == NULL) continue;

        kern_task_t *prev = NULL;
        kern_task_t *t = cls->task_list;
        while (t != NULL) {
            if (t->state == KERN_TASK_ZOMBIE && !(t->flags & KERN_TASK_FLAG_VIRTUAL)) {
                kern_task_t *zombie = t;
                t = t->next;
                if (prev) prev->next = t;
                else cls->task_list = t;
                if (g_last_picked == zombie) g_last_picked = NULL;
                /* 同步更新 g_task_list（如为此 list） */
                if (g_task_list == zombie) g_task_list = t;
                /* 同步更新调度类尾指针，避免后续 enqueue 使用已释放节点 */
                if (cls->task_list_tail == zombie) {
                    cls->task_list_tail = prev;
                }
                /* Native 后端将资源释放推迟到此处：任务已脱离链表，不会再运行，
                 * 可以安全释放其栈等资源。其他后端 kern_exit/kill 已释放完毕，
                 * resource_head 为 NULL，此处调用安全且幂等。 */
                if (zombie->resource_head != NULL) {
                    kern_resource_release_all(zombie);
                }
                g_task_count--;
                kern_log(KERN_LOG_DEBUG, "reaped zombie %d (%s)", zombie->pid, zombie->name);
                free(zombie);
            } else {
                prev = t;
                t = t->next;
            }
        }
    }

#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif

    /* 重建全局尾指针（O(n)，仅在 zombie 回收时执行，频率极低） */
    if (g_task_list == NULL) {
        g_task_list_tail = NULL;
    } else {
        kern_task_t *tail = g_task_list;
        while (tail->next != NULL) tail = tail->next;
        g_task_list_tail = tail;
    }
}

/* ═══ 任务入口蹦床（Native） ═══ */

#ifdef NATIVE_TEST

void task_entry_trampoline(void)
{
    kern_task_t *task = g_switch_to_task;

    if (task == NULL || task->entry == NULL) {
        setcontext(&g_sched_ctx);
        return;
    }

    task->entry(task->arg);

    /* 任务自然返回时统一走 kern_exit() 释放资源 */
    kern_exit();
}
#endif /* NATIVE_TEST */
