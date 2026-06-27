#ifndef USER_ITEM_CONTRACT_H
#define USER_ITEM_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief user_item 初始化回调类型
 */
typedef void (*user_item_init_fn_t)(void *ud);

/**
 * @brief user_item 每帧循环回调类型
 */
typedef void (*user_item_loop_fn_t)(void *ud);

/**
 * @brief user_item 退出回调类型
 */
typedef void (*user_item_exit_fn_t)(void *ud);

/**
 * @brief user_item 注册表项
 * @details 将显示名称与生命周期回调绑定，用于菜单注册。
 */
typedef struct {
    const char *name;            /**< 菜单显示名称 */
    user_item_init_fn_t init;    /**< 初始化回调 */
    user_item_loop_fn_t loop;    /**< 主循环回调 */
    user_item_exit_fn_t exit;    /**< 退出回调 */
} user_item_contract_t;

#ifdef __cplusplus
}
#endif

#endif /* USER_ITEM_CONTRACT_H */
