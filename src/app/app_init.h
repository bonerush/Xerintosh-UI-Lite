#ifndef APP_INIT_H
#define APP_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 外部状态标志（由 main.cpp 定义） */
extern bool wifi_on;
extern bool bt_on;

/* 构建 UI 菜单树 */
void app_init_ui(void);

/* 初始化 WiFi / 蓝牙管理器 */
void app_init_managers(void);

/* 输入处理（每帧调用） */
void app_input_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
