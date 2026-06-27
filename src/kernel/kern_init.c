/**
 * @file   kern_init.c
 * @brief  Xeros 内核初始化、日志与 panic 系统实现
 * @details 实现内核启动入口 kern_init()、分级日志输出 kern_log()、
 *          致命错误处理 kern_panic()。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_init.h"
#include "kern_task_notify.h"
#include "kern_timer.h"
#include "kern_stats.h"

#include <stdio.h>
#include <string.h>

#ifndef NATIVE_TEST
#include "debug_serial.h"
#endif

/* ═══ 内部状态 ═══ */

static bool g_kern_initialized = false;          /* 内核是否已初始化 */
static kern_log_level_t g_log_level = KERN_LOG_INFO;  /* 当前日志级别 */
static char g_last_panic[128] = {0};             /* 上次 panic 消息 */
static bool g_has_panic = false;                 /* 是否发生过 panic */
static uint32_t g_init_count = 0;                /* 初始化次数（幂等性） */

#ifndef NATIVE_TEST
static volatile bool g_log_locked = false;        /* 日志自旋锁（替代 FreeRTOS 互斥锁） */
#endif

/* ═══ 初始化 ═══ */

void kern_init(void)
{
    if (g_kern_initialized) {
        g_init_count++;
        return;
    }

    g_kern_initialized = true;
    g_init_count = 1;

    kern_task_notify_init();
    kern_timer_init();
    kern_stats_init();

    kern_log(KERN_LOG_INFO, "Xeros kernel initialized");
}

/* ═══ 日志系统 ═══ */

void kern_log_set_level(kern_log_level_t level)
{
    g_log_level = level;
}

kern_log_level_t kern_log_get_level(void)
{
    return g_log_level;
}

static const char *log_level_str(kern_log_level_t level)
{
    switch (level) {
    case KERN_LOG_DEBUG: return "DEBUG";
    case KERN_LOG_INFO:  return "INFO";
    case KERN_LOG_WARN:  return "WARN";
    case KERN_LOG_ERROR: return "ERROR";
    case KERN_LOG_PANIC: return "PANIC";
    default:             return "?";
    }
}

void kern_log(kern_log_level_t level, const char *fmt, ...)
{
    if (level < g_log_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    kern_vlog(level, fmt, args);
    va_end(args);
}

void kern_vlog(kern_log_level_t level, const char *fmt, va_list args)
{
    if (level < g_log_level) {
        return;
    }

#ifdef NATIVE_TEST
    fprintf(stdout, "[%s] ", log_level_str(level));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
#else
    /* 硬件环境：输出到串口（自旋锁保护，避免与 FreeRTOS WiFi/BT 任务竞争） */
    while (__sync_lock_test_and_set(&g_log_locked, true)) {
        /* 自旋等待：自旋锁持有者不应长时间阻塞，短暂自旋即可 */
    }
    debug_printf("[%s] ", log_level_str(level));
    debug_vprintf(fmt, args);
    debug_printf("\n");
    __sync_lock_release(&g_log_locked);
#endif
}

/* ═══ Panic ═══ */

void kern_panic(const char *msg)
{
    if (msg != NULL) {
        strncpy(g_last_panic, msg, sizeof(g_last_panic) - 1);
        g_last_panic[sizeof(g_last_panic) - 1] = '\0';
    } else {
        g_last_panic[0] = '\0';
    }
    g_has_panic = true;

    kern_log(KERN_LOG_PANIC, "KERNEL PANIC: %s", msg ? msg : "(no message)");

#ifndef NATIVE_TEST
    /* 硬件环境：panic 后无限循环，LED 闪烁 */
    while (1) {
        /* TODO: 硬件 LED 闪烁 */
    }
#endif
}

void kern_clear_panic(void)
{
    g_has_panic = false;
    g_last_panic[0] = '\0';
}
