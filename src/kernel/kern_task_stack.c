/**
 * @file   kern_task_stack.c
 * @brief  Xeros 任务栈管理实现
 * @details 栈分配/初始化、金丝雀写入、栈使用量查询。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_sched.h"
#include "kern_task.h"
#include "kern_kmalloc.h"
#include "kern_init.h"
#include "kern_port.h"

#include <string.h>
#include <stdlib.h>

#if defined(NATIVE_TEST)
#include <ucontext.h>
#endif

/* Native 后端使用的外部蹦床函数 */
#if defined(NATIVE_TEST)
extern void task_entry_trampoline(void);
#endif

/* ═══ 栈初始化 ═══ */

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)

void task_stack_init(kern_task_t *task, size_t stack_size)
{
    if (stack_size > KERN_STACK_MAX) stack_size = KERN_STACK_MAX;
    if (stack_size < KERN_STACK_MIN) stack_size = KERN_STACK_MIN;

    task->stack_size = stack_size;
    task->stack_base = (uint8_t *)kern_kmalloc_for_task(task, stack_size);
#ifdef XEROS_NATIVE_SCHED
    task->native_stack = task->stack_base;
#endif
    if (task->stack_base == NULL) {
        kern_log(KERN_LOG_WARN, "stack alloc failed for task %s, size=%zu",
                 task->name, stack_size);
        return;
    }

    memset(task->stack_base, 0xAA, stack_size);
}

void task_write_canary(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return;
    if (task->stack_size >= sizeof(uint32_t)) {
        uint32_t canary = KERN_STACK_CANARY;
        memcpy(task->stack_base, &canary, sizeof(uint32_t));
    }
}

#else /* ═══════════════ ESP32 FreeRTOS backend ═══════════════ */

void task_stack_init(kern_task_t *task, size_t stack_size)
{
    (void)task;
    (void)stack_size;
    /* FreeRTOS 分配和管理任务栈 */
}

void task_write_canary(kern_task_t *task)
{
    (void)task;
    /* FreeRTOS 有自己的栈溢出检测 */
}

#endif

/* ═══ 栈使用量查询 ═══ */

#if defined(NATIVE_TEST) || defined(XEROS_NATIVE_SCHED)

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

    if (used == 0) used = 1;
    if (used > task->stack_highwater) {
        task->stack_highwater = used;
    }
    return used;
}

#else /* ESP32 FreeRTOS backend: 通过 uxTaskGetStackHighWaterMark 查询 */

#include <freertos/FreeRTOS.h>

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL) return 0;

    if (task->port_thread != KERN_PORT_THREAD_NULL) {
        /* port 层返回剩余栈字数 */
        size_t free_words = kern_port_thread_stack_usage(task->port_thread);
        size_t free_bytes = free_words * sizeof(StackType_t);

        size_t used = 0;
        if (free_bytes < task->stack_size) {
            used = task->stack_size - free_bytes;
        }
        if (used > task->stack_highwater) {
            task->stack_highwater = used;
        }
        return used;
    }
    return 0;
}

#endif

/* ═══ 栈高水位与推荐接口 ═══ */

size_t kern_task_stack_highwater(kern_task_t *task)
{
    if (task == NULL) return 0;
#if !defined(NATIVE_TEST) && !defined(XEROS_NATIVE_SCHED)
    /* FreeRTOS 路径：通过 port 层查询并同步更新 TCB 字段 */
    if (task->port_thread == KERN_PORT_THREAD_NULL) return 0;
    size_t free_words = kern_port_thread_stack_usage(task->port_thread);
    size_t free_bytes = free_words * sizeof(StackType_t);
    size_t used = 0;
    if (free_bytes < task->stack_size) {
        used = task->stack_size - free_bytes;
    }
    if (used > task->stack_highwater) {
        task->stack_highwater = used;
    }
#endif
    return task->stack_highwater;
}

bool kern_task_stack_overflow_check(kern_task_t *task)
{
    if (task == NULL || task->stack_base == NULL) return false;
    if (task->stack_size < sizeof(uint32_t)) return false;

    uint32_t canary;
    memcpy(&canary, task->stack_base, sizeof(canary));
    if (canary != KERN_STACK_CANARY) {
        kern_log(KERN_LOG_PANIC,
                 "stack overflow detected in task %s (pid=%d)",
                 task->name, task->pid);
        return true;
    }
    return false;
}

size_t kern_task_stack_recommend(kern_task_t *task, size_t current_size)
{
    if (task != NULL && current_size == 0) current_size = task->stack_size;
    if (current_size == 0) current_size = KERN_STACK_MIN;
    if (current_size < KERN_STACK_MIN) current_size = KERN_STACK_MIN;

    size_t peak = (task != NULL) ? task->stack_highwater : 0;
    size_t recommended = peak + KERN_STACK_GROW * 2;
    if (recommended < current_size) recommended = current_size;
    if (recommended > KERN_STACK_MAX) recommended = KERN_STACK_MAX;
    return recommended;
}

/* ═══ 栈自动增长（仅 Native 后端）═══ */

#if defined(NATIVE_TEST)

bool kern_task_stack_grow(kern_task_t *task, size_t new_size)
{
    if (task == NULL || new_size <= task->stack_size) return false;
    if (new_size > KERN_STACK_MAX) new_size = KERN_STACK_MAX;
    if (task == g_current_task) {
        kern_log(KERN_LOG_WARN,
                 "stack_grow: cannot grow running task %s", task->name);
        return false;
    }

    uint8_t *old_base = task->stack_base;
    size_t old_size = task->stack_size;

    uint8_t *new_base = (uint8_t *)kern_kmalloc_for_task(task, new_size);
    if (new_base == NULL) {
        kern_log(KERN_LOG_WARN,
                 "stack_grow: alloc failed for task %s size=%zu",
                 task->name, new_size);
        return false;
    }

    /* 新栈填充 canary 模式，并复制旧栈全部内容到新基址 */
    memset(new_base, 0xAA, new_size);
    if (old_base != NULL && old_size > 0) {
        memcpy(new_base, old_base, old_size);
    }

    task->stack_base = new_base;
    task->stack_size = new_size;
    task->stack_highwater = 0;   /* 增长后重新累计 */

    task->ctx.uc_stack.ss_sp = new_base;
    task->ctx.uc_stack.ss_size = new_size;
    g_switch_to_task = task;
    makecontext(&task->ctx, task_entry_trampoline, 0);

    /* makecontext 可能触及栈底，最后重写 canary */
    task_write_canary(task);

    if (old_base != NULL) {
        kern_kfree(old_base);
    }
    return true;
}

#elif defined(XEROS_NATIVE_SCHED)

bool kern_task_stack_grow(kern_task_t *task, size_t new_size)
{
    if (task == NULL || new_size <= task->stack_size) return false;
    if (new_size > KERN_STACK_MAX) new_size = KERN_STACK_MAX;
    if (task == g_current_task) {
        kern_log(KERN_LOG_WARN,
                 "stack_grow: cannot grow running task %s", task->name);
        return false;
    }

    uint8_t *old_base = task->stack_base;
    size_t old_size = task->stack_size;

    uint8_t *new_base = (uint8_t *)kern_kmalloc_for_task(task, new_size);
    if (new_base == NULL) {
        kern_log(KERN_LOG_WARN,
                 "stack_grow: alloc failed for task %s size=%zu",
                 task->name, new_size);
        return false;
    }

    /* 新栈填充 canary 模式，并复制旧栈全部内容到新基址 */
    memset(new_base, 0xAA, new_size);
    if (old_base != NULL && old_size > 0) {
        memcpy(new_base, old_base, old_size);
    }

    task->stack_base = new_base;
    task->native_stack = new_base;
    task->stack_size = new_size;
    task->stack_highwater = 0;   /* 增长后重新累计 */
    task->native_ctx_valid = false;

    /* xeros_ctx_init 已移除；调度器下次切换到该任务时会通过
     * xeros_task_start 重新从入口启动（任务必须处于非运行态）。 */

    /* xeros_ctx_init 可能触及栈底，最后重写 canary */
    task_write_canary(task);

    if (old_base != NULL) {
        kern_kfree(old_base);
    }
    return true;
}

#else /* ESP32 FreeRTOS backend */

bool kern_task_stack_grow(kern_task_t *task, size_t new_size)
{
    (void)task;
    (void)new_size;
    kern_log(KERN_LOG_WARN,
             "stack_grow: not supported on FreeRTOS backend"
             " (FreeRTOS task stack cannot be resized after creation)");
    return false;
}

#endif

/* ═══ 栈使用画像（按任务名记录历史高水位）═══ */

#define KERN_STACK_PROFILE_MAX 16

static kern_task_stack_profile_t g_stack_profiles[KERN_STACK_PROFILE_MAX];
static size_t                    g_stack_profile_count = 0;

static size_t stack_profile_recommended(size_t highwater)
{
    size_t rec = highwater + KERN_STACK_GROW * 2;
    if (rec < KERN_STACK_MIN) rec = KERN_STACK_MIN;
    if (rec > KERN_STACK_MAX) rec = KERN_STACK_MAX;
    return rec;
}

void kern_task_stack_profile_record(const char *name, size_t highwater)
{
    if (name == NULL) return;

    for (size_t i = 0; i < g_stack_profile_count; i++) {
        if (strncmp(g_stack_profiles[i].name, name, KERN_TASK_NAME_LEN) != 0) {
            continue;
        }
        if (highwater > g_stack_profiles[i].highwater) {
            g_stack_profiles[i].highwater   = highwater;
            g_stack_profiles[i].recommended = stack_profile_recommended(highwater);
        }
        return;
    }

    if (g_stack_profile_count >= KERN_STACK_PROFILE_MAX) {
        kern_log(KERN_LOG_WARN, "stack profile table full, dropping %s", name);
        return;
    }

    kern_task_stack_profile_t *p = &g_stack_profiles[g_stack_profile_count++];
    strncpy(p->name, name, KERN_TASK_NAME_LEN);
    p->name[KERN_TASK_NAME_LEN] = '\0';
    p->highwater   = highwater;
    p->recommended = stack_profile_recommended(highwater);
}

size_t kern_task_stack_recommend_by_name(const char *name, size_t fallback)
{
    if (name == NULL) return stack_profile_recommended(fallback);

    for (size_t i = 0; i < g_stack_profile_count; i++) {
        if (strncmp(g_stack_profiles[i].name, name, KERN_TASK_NAME_LEN) == 0) {
            size_t rec = g_stack_profiles[i].highwater + KERN_STACK_GROW * 2;
            if (rec < fallback) rec = fallback;
            if (rec > KERN_STACK_MAX) rec = KERN_STACK_MAX;
            return rec;
        }
    }

    return stack_profile_recommended(fallback);
}

void kern_task_stack_profile_dump(void (*cb)(const kern_task_stack_profile_t *profile, void *ud),
                                  void *ud)
{
    if (cb == NULL) return;
    for (size_t i = 0; i < g_stack_profile_count; i++) {
        cb(&g_stack_profiles[i], ud);
    }
}
