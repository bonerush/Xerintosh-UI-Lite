#ifndef FLASHER_UI_H
#define FLASHER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef enum {
    FLASHER_UI_LOADING = 0,
    FLASHER_UI_SUCCESS,
    FLASHER_UI_FAILED
} flasher_ui_status_t;

typedef struct {
    flasher_ui_status_t status;
    int progress;        /* 0-100 */
    uint32_t start_ms;   /* for marquee animation */
} flasher_ui_state_t;

void flasher_ui_init(flasher_ui_state_t *st);
void flasher_ui_set_progress(flasher_ui_state_t *st, int pct);
void flasher_ui_set_status(flasher_ui_state_t *st, flasher_ui_status_t status);

/**
 * @brief 渲染一帧进度条 UI
 * @note  全屏显示：白色进度条填充左侧，黑色背景在右侧。
 *        文字居中，根据文字中心点是否在进度条内部决定颜色：
 *        - 内部（白色背景）：黑色文字
 *        - 外部（黑色背景）：白色文字（LOADING）/绿色（SUCCESS）/红色（FAILED）
 *        LOADING 时文字为 "LOADING..."，3 个圆点以 300ms 为周期循环显示 0-3 个。
 */
void flasher_ui_draw(const flasher_ui_state_t *st);

/**
 * @brief 构建跑马灯文字
 * @param buf       输出缓冲区
 * @param buf_size  缓冲区大小
 * @param elapsed_ms 已过去的时间（毫秒）
 * @note  周期 1200ms，每 300ms 增加一个圆点：0→1→2→3→0
 */
void flasher_ui_build_marquee(char *buf, size_t buf_size, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif
