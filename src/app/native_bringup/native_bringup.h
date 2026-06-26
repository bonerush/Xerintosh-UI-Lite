/**
 * @file   native_bringup.h
 * @brief  Xeros 原生调度器 Phase 1 最小测试入口头文件
 *
 * @copyright Copyright (c) 2026
 */

#ifndef XERINTOSH_APP_NATIVE_BRINGUP_H
#define XERINTOSH_APP_NATIVE_BRINGUP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Phase 1 最小原生调度器测试任务
 * @note  创建一个 blink 任务，每 500ms 输出计数。
 */
void native_bringup_init(void);

#ifdef __cplusplus
}
#endif

#endif /* XERINTOSH_APP_NATIVE_BRINGUP_H */
