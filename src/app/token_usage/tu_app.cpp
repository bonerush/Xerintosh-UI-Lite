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
#include "app/wifi/wifi_manager.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "app/ui_service.h"
#include "ui/ui_item.h"

#include <string.h>

/* ═══ 日志宏（NATIVE_TEST 下使用 printf，硬件下使用 ESP_LOG） ═══ */
#ifndef NATIVE_TEST
#include <esp_log.h>
#define TAG "tu_app"
#define TU_LOGD(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#include <stdio.h>
/* Native 测试：直接输出到 stderr */
#define TU_LOGD(...) fprintf(stderr, "[tu_app] " __VA_ARGS__); fputc('\n', stderr)
#endif

/**
 * @brief 安全显示 API key 前 N 位（用于确认 key 是否正确加载）
 * @note  只输出前缀和后缀，不泄露完整 key
 */
static void log_masked_key(const char *key)
{
    if (!key || key[0] == '\0') {
        TU_LOGD("API key: <EMPTY>");
        return;
    }
    size_t len = strlen(key);
    if (len <= 8) {
        TU_LOGD("API key: %.4s**** (len=%zu)", key, len);
    } else {
        TU_LOGD("API key: %.4s...%.4s (len=%zu)", key, key + len - 4, len);
    }
}

/* ═══ 全局状态（文件作用域）═══ */

static tu_data_t g_tu_data;
static uint32_t  g_last_refresh = 0;
static bool      g_needs_refresh = true;

/* ═══ 生命周期 ═══ */

const tu_data_t* token_usage_get_data(void)
{
    return &g_tu_data;
}

void token_usage_init(void *ud)
{
    (void)ud;
    tu_data_init(&g_tu_data);
    g_last_refresh  = 0;
    g_needs_refresh = true;
#ifndef NATIVE_TEST
    ui_service_user_item_init();
#endif
}

void token_usage_loop(void *ud)
{
    (void)ud;

    /* ── 第一步：读取按键事件 ── */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* BtnA 短按：手动刷新 */
    if (event_a == HAL_EVENT_SHORT_PRESS) {
        TU_LOGD("[触发] BtnA 短按 → 手动刷新");
        g_needs_refresh = true;
    }

    /* 长按 B 退出 */
    if (ui_user_item_try_exit(event_b)) return;

    /* ── 第二步：定时刷新或手动刷新 ── */
    uint32_t now = hal_get_ticks();
    if (g_needs_refresh || (now - g_last_refresh >= TU_REFRESH_INTERVAL)) {
        uint32_t cause = (g_needs_refresh) ? now : g_last_refresh;
        TU_LOGD("[BEGIN] 刷新触发 (cause=%s, now=%lu, last=%lu, diff=%lu)",
                 g_needs_refresh ? "MANUAL" : "TIMER",
                 (unsigned long)now, (unsigned long)g_last_refresh,
                 (unsigned long)(now - g_last_refresh));

        /* ── 步骤 A：获取 API key ──
         *  storage 可能为空（未设置 key），此时直接跳过请求 */
        char ds_key[STORAGE_API_KEY_MAX_LEN];
        memset(ds_key, 0, sizeof(ds_key));
        bool has_key = storage_get_deepseek_key(ds_key, sizeof(ds_key));
        TU_LOGD("[A] storage_get_deepseek_key: has_key=%d, first_byte='%c' (0x%02x)",
                 has_key, ds_key[0] >= 32 ? ds_key[0] : '.', (unsigned char)ds_key[0]);
        if (has_key && ds_key[0] != '\0') {
            log_masked_key(ds_key);
        } else {
            TU_LOGD("[A] → API key 不可用 (has_key=%d, empty=%d)",
                     has_key, ds_key[0] == '\0');
        }

        /* ── 步骤 B：检查 WiFi 连接状态 ── */
        bool wifi_ok = wifi_mgr_is_connected();
        TU_LOGD("[B] wifi_mgr_is_connected() = %d", wifi_ok);

        /* ── 决策：刷新数据（空 key 或 WiFi 未连接时跳过请求） ── */
        if (has_key && ds_key[0] != '\0' && wifi_ok) {
            TU_LOGD("[C] → 条件满足，发起网络请求");

            /* ── 步骤 C：确保 DNS 配置 ── */
            wifi_mgr_ensure_dns();
            TU_LOGD("[C] wifi_mgr_ensure_dns() done");

            /* ── 步骤 D：执行 HTTP 请求 ── */
            bool fetch_ok = tu_api_fetch_deepseek(ds_key, &g_tu_data.deepseek);
            g_tu_data.deepseek_ok = fetch_ok;
            TU_LOGD("[D] tu_api_fetch_deepseek() = %d", fetch_ok);
        } else {
            /* ── 记录跳过原因 ── */
            if (!has_key) {
                TU_LOGD("[SKIP] API key 未设置（请通过串口命令 'dskey <your_key>' 设置）");
            } else if (ds_key[0] == '\0') {
                TU_LOGD("[SKIP] API key 为空字符串");
            } else if (!wifi_ok) {
                TU_LOGD("[SKIP] WiFi 未连接，跳过网络请求");
            }
            g_tu_data.deepseek_ok = false;
        }
        g_tu_data.last_update = now;

        g_last_refresh  = now;
        g_needs_refresh = false;
        TU_LOGD("[END] 刷新完成, deepseek_ok=%d", g_tu_data.deepseek_ok);
    }

    /* 第三步：绘制 UI */
    tu_ui_draw(&g_tu_data, 0);
}

void token_usage_exit(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    ui_service_user_item_exit();
#endif
    /* 当前无需清理资源 */
}
