/**
 * @file   ui_item_camera.c
 * @brief  相机绑定
 * @details 实现视图相机与选择器的绑定，确保选择器始终处于可视区域。
 *
 * @copyright Copyright (c) 2026
 */

#include "ui_item.h"
#include <stddef.h>

/* ═══ 相机 ═══ */

xerintosh_camera_t g_xerintosh_camera = {0, 0, 0, 0}; /* 在 refresh 中加上 camera 的坐标 */

/**
 * @brief  将选择器绑定到相机
 * @param  _selector 选择器指针
 * @note   绑定后相机会跟随选择器移动，确保其始终处于可视区域
 */
void xerintosh_bind_selector_to_camera(xerintosh_selector_t *_selector)
{
  if (_selector == NULL) return;

  g_xerintosh_camera.selector = _selector;  /* 坐标在 refresh 内部更新 */
}
