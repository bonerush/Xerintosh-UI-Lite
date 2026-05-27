/**
 * @file   taskmgr.h
 * @brief  任务管理器 App 头文件
 * @details 定义 user_item 生命周期接口，供 app_init.c 注册到菜单。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef TASKMGR_H
#define TASKMGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

void taskmgr_init(void);
void taskmgr_loop(void);
void taskmgr_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* TASKMGR_H */
