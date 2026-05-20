#ifndef APP_INIT_H
#define APP_INIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 外部状态标志（由 main.cpp / native_main.cpp 定义） ─── */

extern bool wifi_on;
extern bool bt_on;

/* ─── 生命周期 ─── */

void app_init_ui(void);
void app_init_managers(void);

/* ─── 每帧输入处理 ─── */

void app_input_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
