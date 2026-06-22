/**
 * @file   debug_serial.cpp
 * @brief  C-compatible debug serial implementation
 */

#include "debug_serial.h"

#ifndef NATIVE_TEST

#include <stdio.h>

void debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

void debug_vprintf(const char *fmt, va_list args)
{
    vprintf(fmt, args);
    fflush(stdout);
}

#else /* NATIVE_TEST */

#include <stdio.h>

void debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void debug_vprintf(const char *fmt, va_list args)
{
    vprintf(fmt, args);
}

#endif
