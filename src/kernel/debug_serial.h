/**
 * @file   debug_serial.h
 * @brief  C-compatible debug serial output
 * @details Provides debug_printf() callable from both C and C++ code,
 *          using stdout/vprintf internally. Output appears on the
 *          default console UART (UART0).
 */
#ifndef DEBUG_SERIAL_H
#define DEBUG_SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>

/** Print a formatted debug string to the hardware serial port.
 *  Always flushes immediately. */
void debug_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/** va_list variant of debug_printf */
void debug_vprintf(const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_SERIAL_H */
