/**
 * @file   sm_ui.c
 * @brief  串口监视器绘制实现
 * @details 实现信息栏（控制按钮 + 波特率）和终端区域的绘制。
 *          Phase 2: 添加入场滑入动画偏移 + 按钮平滑过渡。
 *
 * @copyright Copyright (c) 2026
 */

#include "sm_ui.h"
#include "sm_app.h"
#include "app/settings/settings.h"
#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief  绘制信息栏（顶部控制区）
 * @note   包含 START/STOP 按钮、波特率/连接状态显示、SER/BLE 按钮。
 *         用 XOR 反色矩形实现灵动滑块，复用菜单选择框的 hal_draw_xor_rect，
 *         区域内文字自动反色（白字→黑字，黑底→白底），无需文字裁剪。
 */
static void draw_info_bar(void)
{
    int16_t font_h = hal_get_font_height();
    int16_t bar_h = HAL_ROW_H();
    int16_t entry = (sm_entry_offset < 1.0f) ? 0 : (int16_t)sm_entry_offset;
    int16_t bar_y = HAL_MARGIN_SM + entry;

    const char *start_label = sm_running ? "STOP" : "RUN";
    const char *mode_label = (sm_source == SM_SOURCE_SER) ? "SER" : "BLE";
    int16_t start_w = hal_get_string_width(start_label) + HAL_MARGIN_MD;
    int16_t mode_w  = hal_get_string_width(mode_label) + HAL_MARGIN_MD;

    /* 中间信息：根据数据源显示 */
    char mid_str[20];
    if (sm_source == SM_SOURCE_SER) {
        int32_t baud = settings_serial_baud_hw_value(g_serial_baud_rate);
        snprintf(mid_str, sizeof(mid_str), "RATE:%ld", (long)baud);
    } else {
        /* BLE 模式：显示连接状态 */
        snprintf(mid_str, sizeof(mid_str), "%s",
                 sm_bt_connected ? "BLE:OK" : "BLE:--");
    }
    int16_t mid_w = hal_get_string_width(mid_str);

    int16_t total_w = start_w + mid_w + mode_w;
    int16_t spacing = (SCREEN_WIDTH - HAL_MARGIN_MD * 2 - total_w) / 2;
    if (spacing < HAL_MARGIN_SM) spacing = HAL_MARGIN_SM;

    int16_t start_x = HAL_MARGIN_SM;
    int16_t mid_x   = start_x + start_w + spacing;
    int16_t mode_x  = mid_x + mid_w + spacing;

    int16_t ty = bar_y + (bar_h + font_h) / 2 - 3;
    int16_t start_tx = start_x + (start_w - hal_get_string_width(start_label)) / 2;
    int16_t mode_tx  = mode_x  + (mode_w  - hal_get_string_width(mode_label)) / 2;

    /* 1. 白色文字（所有文字统一绘制，为滑块外底色） */
    hal_draw_string(start_tx, ty, start_label, COLOR_FG);
    hal_draw_string(mid_x, ty, mid_str, COLOR_FG);
    hal_draw_string(mode_tx, ty, mode_label, COLOR_FG);

    /* 2. XOR 反色滑块（复用菜单选择框机制，区域内白字变黑、黑底变白）
     *    alpha 在 [0,100] 范围，确保 xerintosh_animation 逐帧平滑插值 */
    float t = sm_btn_alpha_1 / 100.0f;
    int16_t slider_x = (int16_t)(start_x + (mode_x - start_x) * t);
    int16_t slider_w = (int16_t)(start_w + (mode_w - start_w) * t);
    hal_draw_xor_rect(slider_x + 1, bar_y + 1, slider_w - 2, bar_h - 2);
}

/**
 * @brief  绘制单个文本段（支持 soft-wrap、正常显示）
 * @param  term_y       终端区域顶部 y
 * @param  text_x       文本起始 x（已含前缀偏移）
 * @param  available_w  可用文本宽度
 * @param  segment      文本段
 * @param  prefix_label 前缀（首段有效）
 * @param  prefix_color 前缀颜色
 * @param  first_seg    是否为该缓冲行的首段
 * @param  screen_line  当前屏幕行（输入输出）
 * @param  max_lines    最大屏幕行数
 * @return 更新后的 screen_line
 * @note   宽度 > available_w 时 soft-wrap 折行；否则正常显示。
 */
static int16_t draw_text_segment(int16_t term_y, int16_t text_x,
                                  int16_t available_w, const char *segment,
                                  const char *prefix_label, uint16_t prefix_color,
                                  bool first_seg, int16_t screen_line,
                                  int16_t max_lines)
{
    int16_t font_h = hal_get_font_height();
    int16_t text_w = hal_get_string_width(segment);

    if (text_w > available_w) {
        /* soft-wrap：逐段折行显示 */
        int16_t len = strlen(segment);
        int16_t offset = 0;

        while (offset < len && screen_line < max_lines) {
            int16_t ly = term_y + 1 + (screen_line + 1) * font_h;

            int16_t chars_to_show = 0;
            int16_t i;
            for (i = offset; i < len; i++) {
                char try_buf[SM_TERM_LINE_LEN];
                int16_t try_len = i + 1 - offset;
                if (try_len >= SM_TERM_LINE_LEN) try_len = SM_TERM_LINE_LEN - 1;
                strncpy(try_buf, segment + offset, try_len);
                try_buf[try_len] = '\0';
                if (hal_get_string_width(try_buf) > available_w) {
                    break;
                }
                chars_to_show = try_len;
            }
            if (chars_to_show == 0) {
                chars_to_show = 1; /* 防死循环：至少前进一个字符 */
            }

            char seg_buf[SM_TERM_LINE_LEN];
            strncpy(seg_buf, segment + offset, chars_to_show);
            seg_buf[chars_to_show] = '\0';

            if (offset == 0 && first_seg) {
                hal_draw_string(2, ly, prefix_label, prefix_color);
            }
            hal_draw_string(text_x, ly, seg_buf, COLOR_FG);

            offset += chars_to_show;
            screen_line++;
        }
    } else {
        /* 正常显示 */
        int16_t ly = term_y + 1 + (screen_line + 1) * font_h;
        if (first_seg) {
            hal_draw_string(2, ly, prefix_label, prefix_color);
        }
        hal_draw_string(text_x, ly, segment, COLOR_FG);
        screen_line++;
    }

    return screen_line;
}

/**
 * @brief  绘制终端区域
 * @note   基于字体高度和屏幕尺寸动态计算可见行数和终端区域位置。
 *         支持：soft-wrap 折行、换行符强制换行。
 *         Phase 2: term_y 叠加 sm_entry_offset 入场动画。
 */
static void draw_terminal(void)
{
    int16_t font_h = hal_get_font_height();

    /* Phase 2: 终端区域叠加入场偏移 */
    int16_t entry = (sm_entry_offset < 1.0f) ? 0 : (int16_t)sm_entry_offset;
    int16_t term_y = HAL_HEADER_BOTTOM() + HAL_MARGIN_SM + entry;
    int16_t term_h = SCREEN_HEIGHT - term_y - HAL_MARGIN_SM;

    if (term_h < font_h) term_h = font_h;

    int16_t max_screen_lines = (term_h - HAL_MARGIN_SM) / font_h;  /* 底部留边距 */
    if (max_screen_lines < 1) max_screen_lines = 1;
    if (max_screen_lines > SM_TERM_LINES) max_screen_lines = SM_TERM_LINES;

    hal_draw_rect(0, term_y, SCREEN_WIDTH, term_h, COLOR_FG);

    int16_t max_line_width = SCREEN_WIDTH - HAL_MARGIN_MD * 2;
    int16_t prefix_w = hal_get_string_width("[Master]:");
    int16_t text_x = HAL_MARGIN_SM + prefix_w;
    int16_t available_w = max_line_width - prefix_w;
    if (available_w < 0) available_w = 0;

    int16_t screen_line = 0;

    for (int16_t buf_offset = sm_buffer.scroll;
         buf_offset < sm_buffer.count && screen_line < max_screen_lines;
         buf_offset++) {
        char line_buf[SM_TERM_LINE_LEN];
        sm_buffer_get_line(&sm_buffer, buf_offset, line_buf, sizeof(line_buf));
        if (line_buf[0] == '\0') {
            screen_line++;
            continue;
        }

        bool from_host = sm_buffer_get_line_source(&sm_buffer, buf_offset);
        const char *prefix_label;
        uint16_t prefix_color;
        if (sm_source == SM_SOURCE_BLE) {
            prefix_label = from_host ? "[BT-RX]:" : "[BT-TX]:";
            prefix_color = from_host ? COLOR_ACCENT : COLOR_RED;
        } else {
            prefix_label = from_host ? "[Master]:" : "[Slave]:";
            prefix_color = from_host ? COLOR_RED : COLOR_ACCENT;
        }

        /* 复制到可写缓冲区，以便处理换行符 */
        char temp_buf[SM_TERM_LINE_LEN];
        strncpy(temp_buf, line_buf, sizeof(temp_buf) - 1);
        temp_buf[sizeof(temp_buf) - 1] = '\0';

        char *segment = temp_buf;
        bool first_segment = true;

        /* 遍历所有由 \n 分割的段 */
        while (segment != NULL && screen_line < max_screen_lines) {
            char *nl = strchr(segment, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }

            screen_line = draw_text_segment(term_y, text_x, available_w,
                                             segment, prefix_label, prefix_color,
                                             first_segment, screen_line,
                                             max_screen_lines);
            first_segment = false;

            if (nl != NULL) {
                segment = nl + 1;
            } else {
                segment = NULL;
            }
        }
    }
}

/**
 * @brief 绘制串口监视器完整界面（信息栏 + 终端）
 */
void serial_monitor_draw(void)
{
    draw_info_bar();
    draw_terminal();
}
