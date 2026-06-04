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
#elif defined(XEROS_NATIVE_SCHED)
#include "kern_ctx_esp32.h"
#include <setjmp.h>
#endif

/* ═══ 任务创建 ═══ */

#ifdef NATIVE_TEST

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_sched_init();

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    task->cpu_id = KERN_CPU_ANY;
#ifdef CONFIG_SMP_ENABLED
    task->cpu_id = kern_smp_migrate_assign();
#endif
    task->entry = entry;
    task->arg = arg;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_STACK_MIN;
    task_stack_init(task, stack_sz);

    if (getcontext(&task->ctx) < 0) {
        kern_log(KERN_LOG_WARN, "getcontext failed for task %s", task->name);
        free(task->stack_base);
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

    /* 挂载到任务链表尾部 */
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        while (t->next != NULL) t = t->next;
        t->next = task;
    }
    g_task_count++;
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#elif defined(XEROS_NATIVE_SCHED)

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_sched_init();

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    task->cpu_id = KERN_CPU_ANY;
#ifdef CONFIG_SMP_ENABLED
    task->cpu_id = kern_smp_migrate_assign();
#endif
    task->entry = entry;
    task->arg = arg;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    /* 手动分配栈 + setjmp/longjmp 上下文 */
    task_stack_init(task, (stack_min > 0) ? stack_min : KERN_STACK_MIN);

    uint8_t *stack_top = task->stack_base + task->stack_size;
    kern_ctx_init(&task->ctx, task->stack_base, stack_top, entry, arg);
    task_write_canary(task);

    kern_mpu_setup_stack_guard(task, task->stack_base, task->stack_size);

    /* 插入任务链表（头部插入，保证 idle 保持在最前面） */
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        /* 跳过 idle（pid 0），其余从 idle 之后插入 */
        while (t->next != NULL && t->next->pid == 0) t = t->next;
        task->next = t->next;
        t->next = task;
    }
    g_task_count++;

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_sched_init();

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
    task->timeslice_remaining = SCHED_RR_DEFAULT_TIMESLICE;
    task->cpu_id = KERN_CPU_ANY;
#ifdef CONFIG_SMP_ENABLED
    task->cpu_id = kern_smp_migrate_assign();
#endif
    task->entry = entry;
    task->arg = arg;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task_%d", task->pid);
    }

    /* 通过可移植层创建底层线程承载此 Xeros 任务 */
    size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_PORT_STACK_MIN;
    if (stack_sz < KERN_PORT_STACK_MIN) stack_sz = KERN_PORT_STACK_MIN;

    /* 记录栈信息用于调试 */
    task->stack_size = stack_sz;

    task->port_thread = kern_port_thread_spawn(
        NULL,                              /* entry 由 port 层 task_wrapper 内部读取 task->entry */
        task,                              /* arg = TCB */
        name ? name : "xtask",
        stack_sz,
        task
    );
    if (task->port_thread == KERN_PORT_THREAD_NULL) {
        kern_log(KERN_LOG_WARN, "port: thread spawn failed for %s", task->name);
        free(task);
        return KERN_ENOMEM;
    }

    /* 挂载到任务链表尾部 */
#ifdef CONFIG_SMP_ENABLED
    while (__sync_lock_test_and_set(&g_task_list_lock, true)) {
        asm volatile ("nop");
    }
#endif
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        while (t->next != NULL) t = t->next;
        t->next = task;
    }
    g_task_count++;
#ifdef CONFIG_SMP_ENABLED
    __sync_lock_release(&g_task_list_lock);
#endif

    kern_mpu_setup_stack_guard(task, task->stack_base, task->stack_size);

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#endif

/* ═══ Yield ═══ */

#ifdef NATIVE_TEST

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;
    g_current_task = g_idle_task;

    swapcontext(&cur->ctx, &g_sched_ctx);
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;
    g_current_task = g_idle_task;

    /* setjmp 保存当前任务上下文，longjmp 回到调度器 */
    if (setjmp(cur->ctx.jmp) == 0) {
        longjmp(g_sched_ctx.jmp, 1);
    }
    /* setjmp 返回 1：调度器重新给了我们 CPU */
}

#else /* ESP32: yield = 通过可移植层释放 CPU */

void kern_yield(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_READY;

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

    kern_resource_release_all(cur);

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;
    setcontext(&g_sched_ctx);
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    kern_resource_release_all(cur);

    cur->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) exited", cur->pid, cur->name);

    g_current_task = g_idle_task;

    /* 不保存上下文，直接跳转回调度器（任务已死亡） */
    longjmp(g_sched_ctx.jmp, 1);
}

#else /* ESP32: exit = 标记 ZOMBIE，通过可移植层销毁自身 */

void kern_exit(void)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    kern_resource_release_all(cur);

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

#elif defined(XEROS_NATIVE_SCHED)

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;
    g_current_task = g_idle_task;

    /* 与 kern_yield 相同：保存上下文，跳回调度器 */
    if (setjmp(cur->ctx.jmp) == 0) {
        longjmp(g_sched_ctx.jmp, 1);
    }
}

#else /* ESP32: sleep = yield + 记录唤醒时间 */

void kern_sleep_ms(uint32_t ms)
{
    kern_task_t *cur = g_current_task;
    if (cur == NULL) return;

    cur->state = KERN_TASK_SLEEPING;
    cur->wake_time = g_sched_ticks + ms;

    /* yield 到调度器，调度器会检查 wake_time 决定是否重新调度此任务 */
    kern_yield();
}

#endif

/* ═══ 系统任务保护检查 ═══ */

bool kern_task_is_protected(kern_task_t *task)
{
    if (task == NULL) return false;

    /* 虚任务自身不是系统关键任务，但 taskmgr 自身受保护 */
    static const char *protected_names[] = {
        "idle", "shell", "ui",
        "taskmgr", "任务管理器",  /* 中英文双名保护 */
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

int kern_task_kill(kern_pid_t pid)
{
    kern_task_t *task = kern_task_get(pid);
    if (task == NULL) return KERN_ENOENT;

    /* 不能终止受保护的系统任务 */
    if (kern_task_is_protected(task)) return KERN_EACCES;

    /* 不能终止自身（调用 kern_exit() 代替） */
    if (task == g_current_task) return KERN_EACCES;

    /* 已经是 ZOMBIE 则幂等返回成功 */
    if (task->state == KERN_TASK_ZOMBIE) return 0;

    /* 虚任务：走专门的注销路径（回收 TCB，count--） */
    if (task->flags & KERN_TASK_FLAG_VIRTUAL) {
        kern_task_unregister_virtual(pid);
        return 0;
    }

    /* 非虚任务：先释放追踪资源，再标记 ZOMBIE */
    kern_resource_release_all(task);
    task->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "task %d (%s) killed", task->pid, task->name);

#if defined(NATIVE_TEST)
    /* Native: TCB 在下次 sched_tick 时由 reap_zombies 回收 */
#elif defined(XEROS_NATIVE_SCHED)
    /* 原生调度器：释放手动分配的栈 */
    if (task->stack_base != NULL) {
        free(task->stack_base);
        task->stack_base = NULL;
    }
#else
    /* FreeRTOS: 销毁底层线程 */
    if (task->port_thread != KERN_PORT_THREAD_NULL) {
        kern_port_thread_kill(task->port_thread);
        task->port_thread = KERN_PORT_THREAD_NULL;
    }
#endif

    return 0;
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

    task->state = KERN_TASK_ZOMBIE;
    g_current_task = g_idle_task;
    setcontext(&g_sched_ctx);
}
#endif /* NATIVE_TEST */
