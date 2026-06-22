/**
 * @file   hal_stubs.h
 * @brief  Native 测试环境 HAL 桩访问接口
 * @details 测试代码通过此头文件访问 fake 状态，用于断言验证。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef HAL_STUBS_H
#define HAL_STUBS_H

#ifdef NATIVE_TEST

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ Fake 显示状态 ═══ */

void fake_display_reset(void);
int fake_display_get_pixel_count(void);
int fake_display_get_clear_count(void);
int fake_display_get_flush_count(void);
void fake_display_get_last_pixel(int16_t *x, int16_t *y, uint16_t *color);
uint16_t fake_display_get_last_clear_color(void);
int fake_display_get_char_count(void);
void fake_display_get_last_char(int16_t *x, int16_t *y, char *c, uint16_t *fg, uint16_t *bg);

/* ═══ Fake 输入状态 ═══ */

void fake_input_reset(void);
void fake_input_set_button(char name, bool pressed);

/* ═══ Fake 系统状态 ═══ */

void fake_system_set_ticks(uint32_t ticks);
uint32_t fake_system_get_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_TEST */
#endif /* HAL_STUBS_H */
