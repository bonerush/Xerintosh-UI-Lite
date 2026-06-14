/**
 * @file   tu_app.h
 * @brief  Token Usage App 内部头文件
 * @details 声明刷新间隔等内部常量，供 tu_app.cpp 使用。
 *          公开的生命周期接口见 token_usage.h。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef TU_APP_H
#define TU_APP_H

#include "token_usage.h"
#include "tu_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 内部常量 ═══ */

#define TU_REFRESH_INTERVAL 30000  /* 自动刷新间隔（毫秒） */

/* ═══ 测试可见 getter ═══ */

const tu_data_t* token_usage_get_data(void);

#ifdef __cplusplus
}
#endif

#endif /* TU_APP_H */
