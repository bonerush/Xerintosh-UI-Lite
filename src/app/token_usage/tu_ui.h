/**
 * @file   tu_ui.h
 * @brief  Token Usage UI 渲染模块
 * @details 声明 tu_ui_draw()，用于在全屏 user_item 中绘制
 *          Deepseek / Kimi 的 token 使用状况。
 */

#ifndef TU_UI_H
#define TU_UI_H

#include "tu_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  绘制 Token Usage 全屏界面
 * @param data     指向已填充的 tu_data_t 数据（不可为 NULL）
 * @param selected 当前选中项索引（预留，暂未使用）
 */
void tu_ui_draw(const tu_data_t *data, int selected);

#ifdef __cplusplus
}
#endif

#endif /* TU_UI_H */
