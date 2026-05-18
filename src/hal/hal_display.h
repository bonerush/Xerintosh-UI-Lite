#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 160
#define COLOR_BG      0x0000
#define COLOR_FG      0xFFFF
#define COLOR_ACCENT  0x07E0

extern void hal_display_init(void);
extern void hal_display_clear(void);
extern void hal_display_flush(void);

extern void hal_draw_pixel(int16_t x, int16_t y, uint16_t color);
extern void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
extern void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color);
extern void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color);
extern void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
extern void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
extern void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
extern void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
extern void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color);

extern void hal_set_font(const void* font);
extern void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color);
extern void hal_draw_utf8(int16_t x, int16_t y, const char* str, uint16_t color);
extern int16_t hal_get_string_width(const char* str);
extern int16_t hal_get_utf8_width(const char* str);
extern int16_t hal_get_font_height(void);

extern void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h);
extern void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap);

#ifdef __cplusplus
}
#endif
#endif
