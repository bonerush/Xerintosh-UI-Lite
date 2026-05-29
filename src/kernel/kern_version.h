/**
 * @file   kern_version.h
 * @brief  Xeros 内核版本信息头文件
 * @details 统一定义内核版本号、开发者信息和平台标识。
 *          所有需要显示版本信息的位置（/proc/version、uname 等）
 *          都应引用此头文件，避免版本号散落各处。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_VERSION_H
#define KERN_VERSION_H

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

#endif /* KERN_VERSION_H */
