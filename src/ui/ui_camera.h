/**
 * @file   ui_camera.h
 * @brief  Xerintosh UI 相机头文件
 * @details 定义视图相机结构体及与选择器的绑定接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_CAMERA_H
#define UI_CAMERA_H

#include "ui_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 相机 ═══ */

/**
 * @brief 相机结构体（视图滚动偏移）
 * @note  负责将选择器始终保持在屏幕可视区域内
 */
typedef struct xerintosh_camera_t
{
  float x_camera, x_camera_trg, y_camera, y_camera_trg;  /* 当前与目标偏移 */
  xerintosh_selector_t *selector;  /* 绑定的选择器 */
} xerintosh_camera_t;

extern xerintosh_camera_t g_xerintosh_camera;  /* 全局相机实例 */

/**
 * @brief  将选择器绑定到相机
 * @param  _selector 选择器指针
 */
extern void xerintosh_bind_selector_to_camera(xerintosh_selector_t *_selector);

#ifdef __cplusplus
}
#endif

#endif /* UI_CAMERA_H */
