/**
 * @file   taskmgr.h
 * @brief  任务管理器 App 头文件
 * @details 定义 user_item 生命周期接口和 UI 状态访问函数。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef TASKMGR_H
#define TASKMGR_H

#include "kernel/kern_task.h"
#include "ui/ui_anim_row.h"
#include "hal/hal_layout.h"
#include <stdbool.h>

/* ═══ 布局常量（taskmgr_app.c / taskmgr_ui.c 共享）═══ */

#define TASKMGR_LEFT_MARGIN  HAL_LEFT_X()         /* 标准左缩进 = HAL_MARGIN_MD(4) */
#define TASKMGR_HEADER_H     HAL_ROW_H()           /* 标题栏高度 */
#define TASKMGR_FOOTER_H     HAL_ROW_H()           /* 底部信息栏高度 */
#define TASKMGR_ROW_H        HAL_ROW_H()           /* 列表行高 */

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

void taskmgr_init(void *ud);
void taskmgr_loop(void *ud);
void taskmgr_exit(void *ud);

/* ═══ UI 状态访问（供 taskmgr_ui.c 使用）═══ */

int          taskmgr_visible_lines(void);
int          taskmgr_get_count(void);
int          taskmgr_get_selected(void);
int          taskmgr_get_scroll(void);
kern_task_t *taskmgr_get_task(int index);
bool         taskmgr_is_confirming(void);
bool         taskmgr_is_task_protected(int index);

/* ═══ 动画状态访问（供 taskmgr_ui.c 使用动画坐标）═══ */

const xerintosh_anim_row_list_t *taskmgr_get_anim_list(void);

#ifdef __cplusplus
}
#endif

#endif /* TASKMGR_H */
