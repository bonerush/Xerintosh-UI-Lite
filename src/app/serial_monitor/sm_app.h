/**
 * @file   sm_app.h
 * @brief  串口监视器 App 内部状态头文件
 * @details 声明串口监视器的全局状态变量和动画变量（供 sm_ui.c 等内部模块使用）。
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
extern uint32_t    sm_blink_tick;   /* 闪烁计时（保留向后兼容） */
extern bool        sm_blink_on;     /* 当前闪烁相位（保留向后兼容） */
extern sm_buffer_t sm_buffer;       /* 终端缓冲区 */

/* ═══ 动画状态（Phase 2）═══ */

extern float       sm_entry_offset;   /* 入场滑入偏移（SCREEN_HEIGHT → 0） */
extern float       sm_btn_alpha_0;    /* 按钮 0 高亮度 (0.0~1.0) */
extern float       sm_btn_alpha_1;    /* 按钮 1 高亮度 (0.0~1.0) */

#ifdef __cplusplus
}
#endif

#endif /* SM_APP_H */
