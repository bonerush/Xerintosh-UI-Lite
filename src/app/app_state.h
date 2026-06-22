/**
 * @file   app_state.h
 * @brief  App 层全局状态集中声明
 * @details 收敛 WiFi 开关状态、设置变更回调等原本散落于 main.cpp、
 *          app_init.c、各管理器中的全局符号，降低模块间隐式耦合。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 无线连接开关状态 ═══ */

extern bool g_wifi_on;  /**< WiFi 开关状态，默认开启 */

/* ═══ 设置变更回调（由 main.cpp / native_main.cpp 实现）═══ */

extern void on_brightness_change_cb(void *ud);
extern void on_anim_speed_change_cb(void *ud);
extern void on_anim_enabled_change_cb(void *ud);
extern void on_screen_rotation_change_cb(void *ud);
extern void on_serial_baud_change_cb(void *ud);
extern void on_spring_mode_change_cb(void *ud);
extern void on_spring_stiffness_change_cb(void *ud);
extern void on_spring_damping_change_cb(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
