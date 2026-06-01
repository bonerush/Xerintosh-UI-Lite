/**
 * @file   ui_service.c
 * @brief  UI 服务统一接口实现
 * @details App 层通过此文件间接调用 UI 核心功能，避免直接依赖 ui_item.h。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_service.h"
#include "ui/ui_item.h"

/* ═══ 默认时长常量 ═══ */

#define UI_SVC_DEFAULT_SUCCESS_SPAN 1500
#define UI_SVC_DEFAULT_ERROR_SPAN   2000
#define UI_SVC_DEFAULT_INFO_SPAN    1500
#define UI_SVC_DEFAULT_LOADING_SPAN 3000

/* ═══ 基础服务调用 ═══ */

void ui_svc_call(ui_svc_type_t type, const char *content, uint16_t span_ms)
{
    switch (type) {
        case UI_SVC_POPUP_SHOW:
            xerintosh_push_pop_up(content, span_ms);
            break;
        case UI_SVC_POPUP_HIDE:
            xerintosh_hide_pop_up();
            break;
        case UI_SVC_INFO_BAR_SHOW:
            xerintosh_push_info_bar(content, span_ms);
            break;
    }
}

/* ═══ 便捷封装 ═══ */

void ui_svc_popup(const char *content, uint16_t span_ms)
{
    xerintosh_push_pop_up(content, span_ms);
}

void ui_svc_popup_hide(void)
{
    xerintosh_hide_pop_up();
}

void ui_svc_info_bar(const char *content, uint16_t span_ms)
{
    xerintosh_push_info_bar(content, span_ms);
}

/* ═══ 语义化快捷调用 ═══ */

void ui_svc_notify_success(const char *content)
{
    xerintosh_push_pop_up(content, UI_SVC_DEFAULT_SUCCESS_SPAN);
}

void ui_svc_notify_error(const char *content)
{
    xerintosh_push_pop_up(content, UI_SVC_DEFAULT_ERROR_SPAN);
}

void ui_svc_notify_info(const char *content)
{
    xerintosh_push_pop_up(content, UI_SVC_DEFAULT_INFO_SPAN);
}

void ui_svc_notify_loading(const char *content)
{
    xerintosh_push_pop_up(content, UI_SVC_DEFAULT_LOADING_SPAN);
}
