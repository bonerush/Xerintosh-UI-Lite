/**
 * @file   kern_task.c
 * @brief  Xeros 协作式多任务调度器实现
 * @details 实现 Round-Robin 协作式调度器。
 *
 *          Native 测试：使用 POSIX ucontext（每任务独立栈 + swapcontext）
 *          ESP32：每个 Xeros 任务包装为一个 FreeRTOS 任务（独立栈），
 *                 通过互斥锁实现协作式调度。Round-Robin 由自定义调度器
 *                 管理，上下文切换委托 FreeRTOS（避免 Xtensa 窗口 ABI 冲突）。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_task.h"
#include "kern_init.h"
#include "kern_port.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef NATIVE_TEST
#include <ucontext.h>
#elif defined(XEROS_NATIVE_SCHED)
/* ESP32 setjmp/longjmp 协作式调度器（实验性） */
#include "kern_ctx_esp32.h"
#include <setjmp.h>
#include <esp_timer.h>
#else
/* ESP32: FreeRTOS 任务容器 + 双信号量协作调度（默认）
 * 所有 FreeRTOS 调用已封装到 kern_port.c */
#endif

/* ═══ IDLE 任务配置 ═══ */

#define MAX_TASKS          KERN_MAX_TASKS
#define IDLE_STACK_MIN     2048   /* idle FreeRTOS 任务栈（需容纳 printf 调用） */

/* ═══ 全局调度状态 ═══ */

static kern_task_t   *g_task_list = NULL;      /* 任务链表头 */
static kern_task_t   *g_current_task = NULL;   /* 当前运行的任务 */
static kern_task_t   *g_idle_task = NULL;      /* idle 任务（兜底） */
static kern_task_t   *g_last_picked = NULL;    /* 上一轮选中的任务（Round-Robin 扫描起点） */
static kern_pid_t     g_next_pid = 0;          /* 下一个分配的 PID */
static uint8_t        g_task_count = 0;        /* 任务总数 */
static uint32_t       g_sched_ticks = 0;       /* 调度 tick 计数 */
static bool           g_sched_initialized = false;

#ifdef NATIVE_TEST
/*
 * 调度器上下文
 */
static ucontext_t     g_sched_ctx;

/*
 * makecontext 参数传递：uc_mcontext 只能传 int 参数，
 * 64 位指针通过此全局变量传递。
 */
static kern_task_t   *g_switch_to_task = NULL;
#elif defined(XEROS_NATIVE_SCHED)
/*
 * 调度器上下文 (setjmp/longjmp)
 */
static kern_ctx_t     g_sched_ctx;
#endif

/* ═══ 前向声明 ═══ */

static void idle_entry(void *arg);
static kern_task_t *pick_next_ready(void);
static void reap_zombies(void);
static void task_stack_init(kern_task_t *task, size_t stack_size);
static void task_write_canary(kern_task_t *task);

#ifdef NATIVE_TEST
static void task_entry_trampoline(void);
#else
/* ESP32: FreeRTOS 任务容器 + 双信号量协作调度（默认）
 * 所有 FreeRTOS 调用已封装到 kern_port.c
 * 调度协议见 kern_port.c 的 task_wrapper */
#endif

/* ═══ 初始化 ═══ */

#ifdef NATIVE_TEST

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    task_stack_init(g_idle_task, IDLE_STACK_MIN);

    if (getcontext(&g_idle_task->ctx) < 0) {
        kern_panic("getcontext for idle task failed");
        return;
    }
    g_idle_task->ctx.uc_stack.ss_sp    = g_idle_task->stack_base;
    g_idle_task->ctx.uc_stack.ss_size  = g_idle_task->stack_size;
    g_idle_task->ctx.uc_stack.ss_flags = 0;
    g_idle_task->ctx.uc_link = NULL;

    g_switch_to_task = g_idle_task;
    makecontext(&g_idle_task->ctx, task_entry_trampoline, 0);
    task_write_canary(g_idle_task);

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;  /* 首轮从 idle 之后开始扫描 */

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (native)");
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    /* 创建 idle 任务（使用手动栈 + setjmp/longjmp） */
    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    task_stack_init(g_idle_task, IDLE_STACK_MIN);

    uint8_t *stack_top = g_idle_task->stack_base + g_idle_task->stack_size;
    kern_ctx_init(&g_idle_task->ctx, g_idle_task->stack_base, stack_top,
                  idle_entry, NULL);
    task_write_canary(g_idle_task);

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-native-sched)");
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器 + 协作锁) ═══════════════ */

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;

    /* 初始化可移植层（创建调度基础设施） */
    kern_port_init();

    /* 创建 idle 任务 */
    g_idle_task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (g_idle_task == NULL) {
        kern_panic("failed to allocate idle task");
        return;
    }

    g_idle_task->pid = g_next_pid++;
    g_idle_task->state = KERN_TASK_READY;
    g_idle_task->priority = 0;
    strncpy(g_idle_task->name, "idle", KERN_TASK_NAME_LEN);
    g_idle_task->entry = idle_entry;
    g_idle_task->arg = NULL;
    g_idle_task->stack_size = IDLE_STACK_MIN;

    /* 通过可移植层创建底层线程 */
    g_idle_task->port_thread = kern_port_thread_spawn(
        NULL,                    /* entry 为 NULL：使用 task_wrapper（由 port 层管理） */
        g_idle_task,             /* arg = TCB */
        "xidle",
        IDLE_STACK_MIN,
        g_idle_task
    );
    if (g_idle_task->port_thread == KERN_PORT_THREAD_NULL) {
        kern_panic("failed to create idle thread");
        free(g_idle_task);
        return;
    }

    g_task_list = g_idle_task;
    g_task_count = 1;
    g_current_task = g_idle_task;
    g_last_picked = g_idle_task;  /* 首轮从 idle 之后开始扫描 */

    kern_log(KERN_LOG_DEBUG, "scheduler initialized (esp32-freertos-wrapper)");
}

#endif

/* ═══ Tick ═══ */

#ifdef NATIVE_TEST

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    /* 栈使用率监控：超过 75% 时警告 */
    if (g_current_task != NULL && g_current_task != g_idle_task
        && (g_sched_ticks % 500) == 0) {  /* 每 500 tick 检查一次 */
        size_t usage = kern_task_stack_usage(g_current_task);
        if (usage > 0 && g_current_task->stack_size > 0
            && usage > g_current_task->stack_size * 3 / 4) {
            kern_log(KERN_LOG_WARN,
                     "task %s stack usage %zu/%zu (>75%%)",
                     g_current_task->name,
                     usage,
                     g_current_task->stack_size);
        }
    }

    kern_task_t *volatile next = pick_next_ready();
    if (next == NULL) return;

    g_current_task = next;
    g_switch_to_task = next;
    swapcontext(&g_sched_ctx, &next->ctx);
}

#elif defined(XEROS_NATIVE_SCHED)

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    kern_task_t *next = pick_next_ready();

    if (next == NULL) {
        if (g_sched_ticks % 1000 == 0) {
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        }
        kern_port_idle();
        return;
    }

    g_current_task = next;
    if (next->state != KERN_TASK_SLEEPING) {
        next->state = KERN_TASK_RUNNING;
    }

    /* setjmp 保存调度器上下文，longjmp 切换到目标任务 */
    if (setjmp(g_sched_ctx.jmp) == 0) {
        longjmp(next->ctx.jmp, 1);  /* 跳转到任务的上次 setjmp 点 */
    }
    /* 任务 yield/exit 后回到此处 */
}

#else /* ESP32: 协作式 tick，通过可移植层释放令牌给下一个任务 */

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    g_sched_ticks++;

    reap_zombies();  /* 回收 ZOMBIE 任务 TCB */

    kern_task_t *next = pick_next_ready();
    
    if (next == NULL) {
        /* 无就绪任务，通过可移植层短暂让出 CPU */
        if (g_sched_ticks % 1000 == 0) {
            kern_log(KERN_LOG_WARN, "sched tick=%u: no ready tasks", g_sched_ticks);
        }
        kern_port_idle();
        return;
    }

    g_current_task = next;
    if (next->state != KERN_TASK_SLEEPING) {
        next->state = KERN_TASK_RUNNING;
    }

    /* 通过可移植层切换到目标任务（阻塞等待任务 yield/exit） */
    kern_port_switch_to(next);
}

#endif

/* ═══ 任务创建 ═══ */

#ifdef NATIVE_TEST

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    if (!g_sched_initialized) {
        kern_sched_init();
    }

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
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

    /* 挂载到任务链表尾部 */
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        while (t->next != NULL) t = t->next;
        t->next = task;
    }
    g_task_count++;

    kern_log(KERN_LOG_DEBUG, "spawned task %d: %s", task->pid, task->name);
    return task->pid;
}

#elif defined(XEROS_NATIVE_SCHED)

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (entry == NULL) return KERN_EINVAL;
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    if (!g_sched_initialized) {
        kern_sched_init();
    }

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
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

    if (!g_sched_initialized) {
        kern_sched_init();
    }

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = 128;
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
    if (g_task_list == NULL) {
        g_task_list = task;
    } else {
        kern_task_t *t = g_task_list;
        while (t->next != NULL) t = t->next;
        t->next = task;
    }
    g_task_count++;

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

/* ═══ 查询接口 ═══ */

kern_task_t *kern_task_current(void)
{
    return g_current_task;
}

uint8_t kern_task_count(void)
{
    return g_task_count;
}

kern_task_t *kern_task_get(kern_pid_t pid)
{
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->pid == pid) return t;
        t = t->next;
    }
    return NULL;
}

kern_task_t *kern_task_list_head(void)
{
    return g_task_list;
}

/* ═══ 虚任务管理 ═══ */

kern_pid_t kern_task_register_virtual(const char *name)
{
    if (g_task_count >= MAX_TASKS) return KERN_ENOSPC;

    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    task->pid = g_next_pid++;
    task->state = KERN_TASK_RUNNING;
    task->flags = KERN_TASK_FLAG_VIRTUAL;
    task->priority = 128;
    task->stack_base = NULL;
    task->stack_size = 0;
    task->entry = NULL;
    task->arg = NULL;

    if (name != NULL) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
        task->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "vtask_%d", task->pid);
    }

    /* 插入任务链表头部 */
    task->next = g_task_list;
    g_task_list = task;
    g_task_count++;

    kern_log(KERN_LOG_DEBUG, "virtual task %d (%s) registered", task->pid, task->name);
    return task->pid;
}

void kern_task_unregister_virtual(kern_pid_t pid)
{
    kern_task_t *task = kern_task_get(pid);
    if (task == NULL) return;
    if (!(task->flags & KERN_TASK_FLAG_VIRTUAL)) {
        kern_log(KERN_LOG_WARN, "task %d is not virtual, refusing unregister", pid);
        return;
    }

    task->state = KERN_TASK_ZOMBIE;
    kern_log(KERN_LOG_DEBUG, "virtual task %d (%s) unregistered", task->pid, task->name);

    /* 立即从链表中移除并回收（虚任务无需等待调度器） */
    kern_task_t *prev = NULL;
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t == task) {
            if (prev != NULL) {
                prev->next = t->next;
            } else {
                g_task_list = t->next;
            }
            if (g_last_picked == task) g_last_picked = NULL;
            g_task_count--;
            free(task);
            return;
        }
        prev = t;
        t = t->next;
    }
}

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

    /* 非虚任务：标记 ZOMBIE */
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

#ifdef NATIVE_TEST

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    #define CANARY_SKIP 8
    size_t scan_start = (task->stack_size > CANARY_SKIP) ?
                        (size_t)CANARY_SKIP : task->stack_size;
    size_t used = 0;
    for (size_t i = scan_start; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary;
        memcpy(&canary, task->stack_base, sizeof(uint32_t));
        if (canary != KERN_STACK_CANARY) {
            kern_log(KERN_LOG_WARN, "stack canary corrupted for task %s (pid=%d)",
                     task->name, task->pid);
        }
    }

    return (used > 0) ? used : 1;
}

uint32_t kern_task_stack_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;
    if (task->stack_size < sizeof(uint32_t)) return 0;

    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(uint32_t));
    return canary;
}

#elif defined(XEROS_NATIVE_SCHED)

/* XEROS_NATIVE_SCHED: 手动栈 + 0xAA canary，与 NATIVE_TEST 相同 */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;

    #define CANARY_SKIP 8
    size_t scan_start = (task->stack_size > CANARY_SKIP) ?
                        (size_t)CANARY_SKIP : task->stack_size;
    size_t used = 0;
    for (size_t i = scan_start; i < task->stack_size; i++) {
        if (task->stack_base[i] != 0xAA) {
            used = task->stack_size - i;
            break;
        }
    }

    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary;
        memcpy(&canary, task->stack_base, sizeof(uint32_t));
        if (canary != KERN_STACK_CANARY) {
            kern_log(KERN_LOG_WARN, "stack canary corrupted for task %s (pid=%d)",
                     task->name, task->pid);
        }
    }

    return (used > 0) ? used : 1;
}

uint32_t kern_task_stack_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return 0;
    if (task->stack_size < sizeof(uint32_t)) return 0;

    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(uint32_t));
    return canary;
}

#else /* ESP32: FreeRTOS 管理栈，无法直接扫描 0xAA */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL) return 0;
    /* 通过可移植层查询栈使用量 */
    if (task->port_thread != KERN_PORT_THREAD_NULL) {
        return kern_port_thread_stack_usage(task->port_thread);
    }
    return 0;
}

uint32_t kern_task_stack_canary(kern_task_t *task)
{
    /* FreeRTOS 任务不用 canary，返回 0 表示无检测 */
    (void)task;
    return 0;
}

#endif

/* ═══ 内部函数（Native） ═══ */

static void idle_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_yield();
    }
}

/* ======= ZOMBIE ======= */

static void reap_zombies(void)
{
    kern_task_t *prev = NULL;
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_ZOMBIE && !(t->flags & KERN_TASK_FLAG_VIRTUAL)) {
            kern_task_t *zombie = t;
            t = t->next;
            if (prev) prev->next = t;
            else g_task_list = t;
            if (g_last_picked == zombie) g_last_picked = NULL;
            g_task_count--;
            kern_log(KERN_LOG_DEBUG, "reaped zombie %d (%s)", zombie->pid, zombie->name);
            free(zombie);
        } else {
            prev = t;
            t = t->next;
        }
    }
}

#ifdef NATIVE_TEST

static void task_entry_trampoline(void)
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

/* ─── 栈初始化 ─── */

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)

static void task_stack_init(kern_task_t *task, size_t stack_size)
{
    if (stack_size > KERN_STACK_MAX) stack_size = KERN_STACK_MAX;
    if (stack_size < KERN_STACK_MIN) stack_size = KERN_STACK_MIN;

    task->stack_size = stack_size;
    task->stack_base = (uint8_t *)malloc(stack_size);
    if (task->stack_base == NULL) {
        kern_log(KERN_LOG_WARN, "stack alloc failed for task %s, size=%zu",
                 task->name, stack_size);
        return;
    }

    memset(task->stack_base, 0xAA, stack_size);
}

static void task_write_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return;
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary = KERN_STACK_CANARY;
        memcpy(task->stack_base, &canary, sizeof(uint32_t));
    }
}

/* ─── Round-Robin 就绪任务选择 ─── */

static kern_task_t *pick_next_ready(void)
{
    uint32_t now = g_sched_ticks;

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描，从上一轮选中任务的下一个开始 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : g_task_list;

    t = start;
    do {
        if (t->state == KERN_TASK_READY) {
            g_last_picked = t;
            return t;
        }
        t = t->next;
        if (t == NULL) {
            t = g_task_list;
        }
    } while (t != start);

    /* 所有任务都不可运行：返回 NULL */
    return NULL;
}

#else /* ═══════════════ ESP32 (FreeRTOS 任务容器) ═══════════════ */

/*
 * task_wrapper 已移至 kern_port.c（可移植层）。
 * 以下为 ESP32 专用的辅助函数。
 */

/* ─── 栈初始化（空实现：FreeRTOS 管理栈）─── */
static void task_stack_init(kern_task_t *task, size_t stack_size)
{
    (void)task;
    (void)stack_size;
    /* FreeRTOS 分配和管理任务栈 */
}

static void task_write_canary(kern_task_t *task)
{
    (void)task;
    /* FreeRTOS 有自己的栈溢出检测 */
}

/* ─── Round-Robin 就绪任务选择 ─── */

static kern_task_t *pick_next_ready(void)
{
    uint32_t now = g_sched_ticks;

    /* 第一遍：唤醒所有到期 sleep 任务 */
    kern_task_t *t = g_task_list;
    while (t != NULL) {
        if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now) {
            t->state = KERN_TASK_READY;
        }
        t = t->next;
    }

    /* 第二遍：Round-Robin 扫描 */
    kern_task_t *start = (g_last_picked != NULL && g_last_picked->next != NULL)
                         ? g_last_picked->next
                         : g_task_list;

    if (start == NULL) {
        return NULL;  /* 任务列表为空 */
    }

    t = start;
    do {
        if (t->state == KERN_TASK_READY) {
            g_last_picked = t;
            return t;
        }
        t = t->next;
        if (t == NULL) {
            t = g_task_list;
        }
    } while (t != start);

    /* 所有任务都不可运行 */
    return NULL;
}

#endif
