/**
 * @file   taskmgr.h
 * @brief  任务管理器 App 头文件
 * @details 定义 user_item 生命周期接口和 UI 状态访问函数。
 *          使用 taskmgr_task_info_t 替代原 Xeros kern_task_t。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef TASKMGR_H
#define TASKMGR_H

#include "ui/ui_anim_row.h"
#include "hal/hal_layout.h"
#include <stdbool.h>

#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

/* ═══ 布局常量（taskmgr_app.c / taskmgr_ui.c 共享）═══ */

#define TASKMGR_LEFT_MARGIN  HAL_LEFT_X()         /* 标准左缩进 = HAL_MARGIN_MD(4) */
#define TASKMGR_HEADER_H     HAL_ROW_H()           /* 标题栏高度 */
#define TASKMGR_FOOTER_H     HAL_ROW_H()           /* 底部信息栏高度 */
#define TASKMGR_ROW_H        HAL_ROW_H()           /* 列表行高 */

/* ═══ 任务信息结构（替代原 Xeros kern_task_t）═══ */

#define TASKMGR_MAX_TASKS    16

/**
 * @brief 任务管理器任务信息结构体
 * @note  替代原 Xeros 内核的 kern_task_t，同时支持 FreeRTOS 原生任务
 *        和 user_item 虚任务。
 */
typedef struct {
    int         index;          /* 列表序号 */
    char        name[16];       /* 任务名（FreeRTOS pcTaskGetName 缓冲区） */
    const char *state_str;      /* 状态字符串（"RUN", "READY", "SLEEP" 等） */
    int         stack_free;     /* 剩余栈字（FreeRTOS usStackHighWaterMark），-1=虚任务无栈 */
    bool        is_protected;   /* 是否受保护（不可终止） */
    bool        is_virtual;     /* 是否为 user_item 虚任务 */
    void       *handle;         /* FreeRTOS TaskHandle_t，虚任务为 NULL */
    bool        active;         /* 此条目是否有效 */
} taskmgr_task_info_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

void taskmgr_init(void *ud);
void taskmgr_loop(void *ud);
void taskmgr_exit(void *ud);

/* ═══ UI 状态访问（供 taskmgr_ui.c 使用）═══ */

int                      taskmgr_visible_lines(void);
int                      taskmgr_get_count(void);
int                      taskmgr_get_selected(void);
int                      taskmgr_get_scroll(void);
const taskmgr_task_info_t *taskmgr_get_task(int index);
bool                     taskmgr_is_confirming(void);
bool                     taskmgr_is_task_protected(int index);

/* ═══ 动画状态访问（供 taskmgr_ui.c 使用动画坐标）═══ */

const xerintosh_anim_row_list_t *taskmgr_get_anim_list(void);

#ifndef NATIVE_TEST
/**
 * @brief 注册一个 FreeRTOS 任务到任务管理器列表
 * @note  在 xTaskCreate 后调用，传入任务句柄和名称。
 */
void taskmgr_register_task(TaskHandle_t handle, const char *name, bool is_protected);
#endif

#ifdef NATIVE_TEST
/**
 * @brief Native 测试桩：向任务列表注入测试数据
 * @note  仅 native 测试环境使用，硬件环境通过 FreeRTOS API 获取。
 */
void taskmgr_test_add_task(const char *name, const char *state_str,
                           int stack_free, bool is_protected, bool is_virtual);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TASKMGR_H */
