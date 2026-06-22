/**
 * @file   token_usage.h
 * @brief  Token Usage App 生命周期接口
 * @details 定义 user_item 三函数生命周期契约（init/loop/exit），
 *          用于在菜单中以全屏 App 方式运行 Token Usage 查看器。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef TOKEN_USAGE_H
#define TOKEN_USAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 生命周期（user_item 接口）═══ */

void token_usage_init(void *ud);
void token_usage_loop(void *ud);
void token_usage_exit(void *ud);

#ifdef __cplusplus
}
#endif

#endif /* TOKEN_USAGE_H */
