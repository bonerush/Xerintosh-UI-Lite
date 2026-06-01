/**
 * @file   ui_service.h
 * @brief  UI 服务统一接口声明
 * @details App 层通过此头文件调用系统 UI 服务（弹窗、信息栏等），
 *          无需直接依赖 ui/ui_item.h 的实现细节。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 服务类型枚举（未来扩展用）═══ */

typedef enum {
    UI_SVC_POPUP_SHOW,    /* 显示中部弹窗 */
    UI_SVC_POPUP_HIDE,    /* 隐藏中部弹窗 */
    UI_SVC_INFO_BAR_SHOW, /* 显示顶部信息栏 */
} ui_svc_type_t;

/* ═══ 基础服务调用 ═══ */

/**
 * @brief 统一 UI 服务调用入口
 * @param type    服务类型
 * @param content 显示文本（POPUP_HIDE 时可为 NULL）
 * @param span_ms 显示持续时间（毫秒，0 表示使用默认值）
 */
extern void ui_svc_call(ui_svc_type_t type, const char *content, uint16_t span_ms);

/* ═══ 便捷封装（推荐 App 直接使用）═══ */

/**
 * @brief 显示弹窗
 * @param content 显示文本
 * @param span_ms 持续时间（毫秒）
 */
extern void ui_svc_popup(const char *content, uint16_t span_ms);

/**
 * @brief 隐藏弹窗
 */
extern void ui_svc_popup_hide(void);

/**
 * @brief 显示顶部信息栏
 * @param content 显示文本
 * @param span_ms 持续时间（毫秒）
 */
extern void ui_svc_info_bar(const char *content, uint16_t span_ms);

/* ═══ 语义化快捷调用（带默认时长）═══ */

/**
 * @brief 成功提示（默认 1500ms）
 */
extern void ui_svc_notify_success(const char *content);

/**
 * @brief 错误提示（默认 2000ms）
 */
extern void ui_svc_notify_error(const char *content);

/**
 * @brief 信息提示（默认 1500ms）
 */
extern void ui_svc_notify_info(const char *content);

/**
 * @brief 加载中提示（默认 3000ms）
 */
extern void ui_svc_notify_loading(const char *content);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H */
