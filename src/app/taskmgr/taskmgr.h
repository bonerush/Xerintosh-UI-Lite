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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

void taskmgr_init(void);
void taskmgr_loop(void);
void taskmgr_exit(void);

/* ═══ UI 状态访问（供 taskmgr_ui.c 使用）═══ */

int          taskmgr_visible_lines(void);
int          taskmgr_get_count(void);
int          taskmgr_get_selected(void);
int          taskmgr_get_scroll(void);
kern_task_t *taskmgr_get_task(int index);
bool         taskmgr_is_confirming(void);
bool         taskmgr_is_task_protected(int index);

#ifdef __cplusplus
}
#endif

#endif /* TASKMGR_H */
