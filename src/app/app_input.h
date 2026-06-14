/**
 * @file   app_input.h
 * @brief  App 每帧输入处理接口
 * @details 负责将硬件按键事件映射到 UI 选择器导航，
 *          并调度各模块状态机（电源键弹窗、WiFi 弹窗、烧录器强制解除等）。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef APP_INPUT_H
#define APP_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理按键输入事件
 */
void app_input_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INPUT_H */
