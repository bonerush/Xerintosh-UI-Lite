/**
 * @file   kern_init.c
 * @brief  Xeros 内核初始化、日志与 panic 系统实现
 * @details 实现内核启动入口 kern_init()、分级日志输出 kern_log()、
 *          致命错误处理 kern_panic() 及统计信息 kern_log_stats()。
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_init.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef NATIVE_TEST
#include <stdio.h>  /* 在 native 环境 printf 输出到 stdout */
#endif

/* ═══ 内部状态 ═══ */

static bool g_kern_initialized = false;          /* 内核是否已初始化 */
static kern_log_level_t g_log_level = KERN_LOG_INFO;  /* 当前日志级别 */
static char g_last_panic[128] = {0};             /* 上次 panic 消息 */
static bool g_has_panic = false;                 /* 是否发生过 panic */
static uint32_t g_init_count = 0;                /* 初始化次数（幂等性） */

/* ═══ 初始化 ═══ */

void kern_init(void)
{
    if (g_kern_initialized) {
        g_init_count++;
        return;
    }

    g_kern_initialized = true;
    g_init_count = 1;

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
    /* 硬件环境：输出到串口 */
    printf("[%s] ", log_level_str(level));
    vprintf(fmt, args);
    printf("\n");
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

const char *kern_get_last_panic(void)
{
    return g_has_panic ? g_last_panic : NULL;
}

void kern_clear_panic(void)
{
    g_has_panic = false;
    g_last_panic[0] = '\0';
}

/* ═══ 统计信息 ═══ */

void kern_log_stats(void)
{
    kern_log(KERN_LOG_INFO, "--- Kernel Stats ---");
    kern_log(KERN_LOG_INFO, "  Initialized: %s", g_kern_initialized ? "yes" : "no");
    kern_log(KERN_LOG_INFO, "  Init count:  %u", g_init_count);
    kern_log(KERN_LOG_INFO, "  Log level:   %s (%d)", log_level_str(g_log_level), (int)g_log_level);
    kern_log(KERN_LOG_INFO, "  Has panic:   %s", g_has_panic ? "yes" : "no");
    kern_log(KERN_LOG_INFO, "--------------------");
}
