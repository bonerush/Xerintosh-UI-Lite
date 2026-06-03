/**
 * @file   tu_app.cpp
 * @brief  Token Usage App 生命周期实现
 * @details 实现 init/loop/exit 三函数生命周期，管理数据刷新
 *          （30 秒定时 + BtnA 手动触发），集成 storage、API、UI 模块。
 *
 * @copyright Copyright (c) 2026
 */

#include "tu_app.h"
#include "tu_api.h"
#include "tu_ui.h"
#include "app/storage/storage.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_item.h"

/* ═══ 全局状态（文件作用域）═══ */

static tu_data_t g_tu_data;
static uint32_t  g_last_refresh = 0;
static bool      g_needs_refresh = true;

/* ═══ 生命周期 ═══ */

void token_usage_init(void *ud)
{
    (void)ud;
    tu_data_init(&g_tu_data);
    g_last_refresh  = 0;
    g_needs_refresh = true;
}

void token_usage_loop(void *ud)
{
    (void)ud;

    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* BtnA 短按：手动刷新 */
    if (event_a == HAL_EVENT_SHORT_PRESS) {
        g_needs_refresh = true;
    }

    /* 长按 B 退出 */
    if (ui_user_item_try_exit(event_b)) return;

    /* 第二步：定时刷新或手动刷新 */
    uint32_t now = hal_get_ticks();
    if (g_needs_refresh || (now - g_last_refresh >= TU_REFRESH_INTERVAL)) {
        /* 获取 API key */
        char ds_key[STORAGE_API_KEY_MAX_LEN];
        char kimi_key[STORAGE_API_KEY_MAX_LEN];
        storage_get_deepseek_key(ds_key, sizeof(ds_key));
        storage_get_kimi_key(kimi_key, sizeof(kimi_key));

        /* 刷新数据 */
        g_tu_data.deepseek_ok = tu_api_fetch_deepseek(ds_key, &g_tu_data.deepseek);
        g_tu_data.kimi_ok     = tu_api_fetch_kimi(kimi_key, &g_tu_data.kimi);
        g_tu_data.last_update = now;

        g_last_refresh  = now;
        g_needs_refresh = false;
    }

    /* 第三步：绘制 UI */
    tu_ui_draw(&g_tu_data, 0);
}

void token_usage_exit(void *ud)
{
    (void)ud;
    /* 当前无需清理资源 */
}
