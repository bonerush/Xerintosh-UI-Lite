/**
 * @file   kern_port_native_test.c
 * @brief  Xeros 内核可移植层 — Native 测试桩
 * @details 为 NATIVE_TEST 桌面构建提供 kern_port_ops_t 空实现。
 *          Native 测试使用 ucontext 直接进行上下文切换，
 *          不需要底层线程/定时器支持。
 *
 * @copyright Copyright (c) 2026
 */

#ifdef NATIVE_TEST

#include "kern_port.h"
#include "kern_task.h"

static void kern_port_native_test_init(void) {}

static kern_port_thread_t kern_port_native_test_thread_spawn(
    void (*entry)(void *arg), void *arg, const char *name,
    size_t stack_size, kern_task_t *task)
{ (void)entry; (void)arg; (void)name; (void)stack_size; (void)task;
  return KERN_PORT_THREAD_NULL; }

static void kern_port_native_test_thread_exit(void) { while(1){} }

static void kern_port_native_test_thread_kill(kern_port_thread_t thread) { (void)thread; }

static size_t kern_port_native_test_thread_stack_usage(kern_port_thread_t thread)
{ (void)thread; return 0; }

static void kern_port_native_test_switch_to(kern_task_t *task) { (void)task; }

static void kern_port_native_test_task_yield(void) {}

static void kern_port_native_test_task_exit(void) { while(1){} }

static void kern_port_native_test_idle(void) {}

static kern_err_t kern_port_native_test_timer_set(uint32_t period_us)
{ return (period_us == 0) ? KERN_EINVAL : KERN_OK; }

static void kern_port_native_test_timer_stop(void) {}

static bool kern_port_native_test_preempt_consume(void) { return false; }

const kern_port_ops_t g_kern_port_ops = {
    .init                = kern_port_native_test_init,
    .thread_spawn        = kern_port_native_test_thread_spawn,
    .thread_exit         = kern_port_native_test_thread_exit,
    .thread_kill         = kern_port_native_test_thread_kill,
    .thread_stack_usage  = kern_port_native_test_thread_stack_usage,
    .switch_to           = kern_port_native_test_switch_to,
    .task_yield          = kern_port_native_test_task_yield,
    .task_exit           = kern_port_native_test_task_exit,
    .idle                = kern_port_native_test_idle,
    .timer_set_periodic  = kern_port_native_test_timer_set,
    .timer_stop          = kern_port_native_test_timer_stop,
    .preempt_consume     = kern_port_native_test_preempt_consume,
};

#endif /* NATIVE_TEST */
