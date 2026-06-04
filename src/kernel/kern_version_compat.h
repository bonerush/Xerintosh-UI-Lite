/**
 * @file   kern_version_compat.h
 * @brief  版本信息兼容头文件（FreeRTOS-version 精简版）
 * @details 从已删除的 kern_version.h 中提取版本宏定义。
 *          此文件是 src/kernel/ 中唯一保留的文件。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_VERSION_COMPAT_H
#define KERN_VERSION_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 版本信息 ═══ */

#define XEROS_VERSION_STRING "0.2.0"

/* ═══ 开发者与项目信息 ═══ */

#define XEROS_DEVELOPER      "Bonerush"
#define XEROS_CODENAME       "M5Stick-P1"

#ifdef NATIVE_TEST
#define XEROS_PLATFORM       "native (x86_64)"
#else
#define XEROS_PLATFORM       "ESP32-PICO"
#endif

#ifdef __cplusplus
}
#endif

#endif /* KERN_VERSION_COMPAT_H */
