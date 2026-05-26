/**
 * @file   sm_app.h
 * @brief  串口监视器 App 生命周期头文件
 * @details 声明串口监视器的生命周期函数及全局状态变量。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SM_APP_H
#define SM_APP_H

#include "sm_buffer.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 全局状态 ═══ */

extern bool        sm_running;      /* 监视器是否正在工作 */
extern bool        sm_debug;        /* DEBUG 模式 */
extern uint8_t     sm_selected;     /* 0 = START/STOP, 1 = NORM/DEBUG */
extern uint32_t    sm_blink_tick;   /* 闪烁计时 */
extern bool        sm_blink_on;     /* 当前闪烁相位 */
extern sm_buffer_t sm_buffer;       /* 终端缓冲区 */

/* ═══ 生命周期（user_item 接口）═══ */

void serial_monitor_init(void);
void serial_monitor_loop(void);
void serial_monitor_exit(void);

/* ═══ 后台更新 ═══ */

void serial_monitor_update(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_APP_H */
