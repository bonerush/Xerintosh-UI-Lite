/**
 * @file   serial_monitor.c
 * @brief  串口监视器 App 实现
 * @details 全屏 user_item App，实现串口数据的实时监视和缓存。
 *          支持 START/STOP 控制、NORM/DEBUG 模式、终端消息上下滚动。
 *          界面适配横竖屏（80x160 竖屏 / 160x80 横屏）。
 *
 * @copyright Copyright (c) 2026
 */

#include "serial_monitor.h"

#include "app/settings.h"
#include "app/serial_input.h"
#include "hal/hal_input.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include <string.h>
#include <stdio.h>

/* ═══ 常量 ═══ */

#define SM_TERM_LINES    20   /* 终端缓冲区最大行数 */
#define SM_TERM_LINE_LEN 64   /* 每行最大字符数 */
#define SM_BLINK_PERIOD  500  /* 反色闪烁周期（毫秒） */

/* ═══ 数据结构 ═══ */

/**
 * @brief 终端单行数据
 */
typedef struct {
    char text[SM_TERM_LINE_LEN];  /* 文本内容 */
    bool from_host;               /* true = 主机接收，false = MCU 发出 */
} sm_line_t;

/**
 * @brief 终端环形缓冲区
 */
typedef struct {
    sm_line_t lines[SM_TERM_LINES];  /* 行数据数组 */
    uint8_t head;                     /* 下一行写入位置 */
    uint8_t count;                    /* 当前有效行数 */
    int16_t scroll;                   /* 滚动偏移（0 = 显示最新） */
} sm_buffer_t;

/* ═══ 状态变量 ═══ */

static bool        sm_running = false;    /* 监视器是否正在工作 */
static bool        sm_debug = false;      /* DEBUG 模式 */
static uint8_t     sm_selected = 0;       /* 0 = START/STOP, 1 = NORM/DEBUG */
static uint32_t    sm_blink_tick = 0;     /* 闪烁计时 */
static bool        sm_blink_on = false;   /* 当前闪烁相位 */
static sm_buffer_t sm_buffer;             /* 终端缓冲区 */

#ifndef NATIVE_TEST
#include <Arduino.h>  /* 提供 Serial 对象 */
static char        sm_rx_buf[SM_TERM_LINE_LEN];  /* 串口接收行缓冲 */
static uint8_t     sm_rx_len = 0;                /* 接收缓冲当前长度 */
#endif

/* ═══ 缓冲区操作（非 static，供 native 测试使用）═══ */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化终端缓冲区
 */
void sm_buffer_init(sm_buffer_t *buf)
{
    memset(buf, 0, sizeof(*buf));
}

/**
 * @brief  向缓冲区添加一行
 * @param  buf       缓冲区指针
 * @param  text      文本内容
 * @param  from_host 是否来自主机
 * @return true 添加成功
 * @note   当缓冲区满时，新行会覆盖最旧的行（环形覆盖）
 */
bool sm_buffer_add_line(sm_buffer_t *buf, const char *text, bool from_host)
{
    if (!buf || !text) return false;

    sm_line_t *line = &buf->lines[buf->head];
    strncpy(line->text, text, SM_TERM_LINE_LEN - 1);
    line->text[SM_TERM_LINE_LEN - 1] = '\0';
    line->from_host = from_host;

    buf->head = (buf->head + 1) % SM_TERM_LINES;
    if (buf->count < SM_TERM_LINES) {
        buf->count++;
    }
    return true;
}

/**
 * @brief  从缓冲区获取指定偏移的行
 * @param  buf      缓冲区指针（const）
 * @param  offset   偏移量（0 = 最新行，1 = 次新行，...）
 * @param  out      输出缓冲区
 * @param  out_len  输出缓冲区大小
 * @note   若 offset 超出有效行数范围，输出空字符串
 */
void sm_buffer_get_line(const sm_buffer_t *buf, int16_t offset,
                         char *out, size_t out_len)
{
    if (!buf || !out || out_len == 0) return;

    out[0] = '\0';
    if (offset < 0 || offset >= buf->count) return;

    int16_t idx = (buf->head - 1 - offset + SM_TERM_LINES) % SM_TERM_LINES;
    const sm_line_t *line = &buf->lines[idx];
    /* 使用单字符前缀节省屏幕宽度，竖屏 80px 下每行约 13 字符 */
    snprintf(out, out_len, "%c%s",
             line->from_host ? '<' : '>',
             line->text);
}

/**
 * @brief 清空缓冲区
 */
void sm_buffer_clear(sm_buffer_t *buf)
{
    if (!buf) return;
    memset(buf, 0, sizeof(*buf));
}

#ifdef __cplusplus
}
#endif

/* ═══ 绘制函数 ═══ */

/**
 * @brief  绘制单个按钮
 * @param  x       左上角 x
 * @param  y       左上角 y
 * @param  w       宽度
 * @param  h       高度
 * @param  label   按钮文字
 * @param  selected 是否被选中
 * @note   选中时以 500ms 周期反色闪烁
 */
static void draw_button(int16_t x, int16_t y, int16_t w, int16_t h,
                        const char *label, bool selected)
{
    uint16_t bg_color, text_color;

    if (selected && sm_blink_on) {
        bg_color = COLOR_FG;   /* 白底 */
        text_color = COLOR_BG; /* 黑字 */
    } else {
        bg_color = COLOR_BG;   /* 黑底 */
        text_color = COLOR_FG; /* 白字 */
    }

    hal_draw_fill_rect(x, y, w, h, bg_color);
    hal_draw_rect(x, y, w, h, COLOR_FG);

    int16_t tw = hal_get_string_width(label);
    int16_t tx = x + (w - tw) / 2;
    int16_t ty = y + (h - hal_get_font_height()) / 2
                 + hal_get_font_height() / 2;
    hal_draw_string(tx, ty, label, text_color);
}

/**
 * @brief  绘制信息栏（顶部控制区）
 * @note   包含 START/STOP 按钮、波特率显示、NORM/DEBUG 按钮
 *         根据 g_is_landscape 选择横竖屏两套坐标
 */
static void draw_info_bar(void)
{
    bool landscape = g_is_landscape;

    /* 布局参数 */
    int16_t btn_h, btn_y, rate_x, rate_y, norm_x, norm_w;

    if (!landscape) {
        /* 竖屏 80x160 */
        btn_h = 10;
        btn_y = 1;
        rate_x = 22;
        rate_y = 3;
        norm_x = 55;
        norm_w = 24;
    } else {
        /* 横屏 160x80 */
        btn_h = 12;
        btn_y = 1;
        rate_x = 30;
        rate_y = 4;
        norm_x = 132;
        norm_w = 27;
    }

    /* START/STOP 按钮（左侧） */
    const char *start_label = sm_running ? "STOP" : "RUN";
    int16_t start_w = landscape ? 24 : 19;
    draw_button(1, btn_y, start_w, btn_h, start_label, sm_selected == 0);

    /* 波特率显示（中间） */
    char rate_str[16];
    int32_t baud = settings_serial_baud_hw_value(g_serial_baud_rate);
    snprintf(rate_str, sizeof(rate_str), "%ld", (long)baud);
    hal_draw_string(rate_x, rate_y, rate_str, COLOR_FG);

    /* NORM/DEBUG 按钮（右侧） */
    const char *mode_label = sm_debug ? "DBG" : "NORM";
    draw_button(norm_x, btn_y, norm_w, btn_h, mode_label, sm_selected == 1);
}

/**
 * @brief  绘制终端区域
 * @note   根据屏幕方向和字体高度计算可见行数，
 *         从缓冲区底部向上取行显示，应用 scroll 偏移
 */
static void draw_terminal(void)
{
    int16_t sh = SCREEN_HEIGHT;
    bool landscape = g_is_landscape;

    int16_t term_y = landscape ? 16 : 14;
    int16_t term_h = sh - term_y;
    int16_t font_h = hal_get_font_height();
    int16_t visible_lines = term_h / font_h;

    if (visible_lines < 1) visible_lines = 1;
    if (visible_lines > SM_TERM_LINES) visible_lines = SM_TERM_LINES;

    /* 终端区域边框 */
    hal_draw_rect(0, term_y, SCREEN_WIDTH, term_h, COLOR_FG);

    /* 逐行显示 */
    char line_buf[SM_TERM_LINE_LEN];
    int16_t max_line_width = SCREEN_WIDTH - 4;  /* 左右各留 2px 边距 */
    for (int16_t i = 0; i < visible_lines; i++) {
        int16_t offset = sm_buffer.scroll + i;
        sm_buffer_get_line(&sm_buffer, offset, line_buf, sizeof(line_buf));
        if (line_buf[0] != '\0') {
            int16_t ly = term_y + 1 + i * font_h + font_h / 2;
            /* 若行内容超出屏幕宽度，截断显示 */
            int16_t lw = hal_get_string_width(line_buf);
            if (lw > max_line_width) {
                /* 从尾部截断，保留前缀 */
                int16_t chars_to_show = 0;
                for (chars_to_show = (int16_t)strlen(line_buf); chars_to_show > 0; chars_to_show--) {
                    line_buf[chars_to_show] = '\0';
                    if (hal_get_string_width(line_buf) <= max_line_width) {
                        break;
                    }
                }
            }
            hal_draw_string(2, ly, line_buf, COLOR_FG);
        }
    }
}

/* ═══ 核心函数 ═══ */

/**
 * @brief 初始化串口监视器 App
 * @note  重置所有状态、清空缓冲区、停止监视
 */
void serial_monitor_init(void)
{
    sm_running = false;
    sm_debug = false;
    sm_selected = 0;
    sm_blink_tick = hal_get_ticks();
    sm_blink_on = false;
    sm_buffer_init(&sm_buffer);

#ifndef NATIVE_TEST
    sm_rx_len = 0;
#endif
}

/**
 * @brief 串口监视器主循环（每帧调用）
 * @note  处理输入事件、更新反色闪烁、绘制信息栏和终端
 *
 * 中文伪代码拆解：
 *
 * 函数 serial_monitor_loop() {
 *     // 第一步：读取按键事件
 *     事件A = hal_input_get_event(BtnA)
 *     事件B = hal_input_get_event(BtnB)
 *
 *     // 第二步：短按切换选择器
 *     if (事件A == 短按 或 事件B == 短按) {
 *         sm_selected = !sm_selected   // 0↔1 切换
 *     }
 *
 *     // 第三步：长按确认按钮切换状态
 *     if (事件A == 长按) {
 *         if (选中 START/STOP)  sm_running = !sm_running
 *         if (选中 NORM/DEBUG)  sm_debug = !sm_debug
 *     }
 *
 *     // 第四步：长按返回按钮退出 App
 *     if (事件B == 长按) {
 *         调用 xerintosh_selector_exit_current_item() 退出
 *         return
 *     }
 *
 *     // 第五步：双击滚动终端
 *     if (事件A == 双击) scroll++   // 向下看更旧消息
 *     if (事件B == 双击) scroll--   // 向上看更新消息
 *
 *     // 第六步：更新反色闪烁相位
 *     if (当前时间 - 上次闪烁时间 >= 500ms) {
 *         切换闪烁相位
 *         更新计时
 *     }
 *
 *     // 第七步：绘制界面
 *     绘制信息栏()
 *     绘制终端()
 * }
 */
void serial_monitor_loop(void)
{
    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 第二步：短按切换选择器 */
    if (event_a == HAL_EVENT_SHORT_PRESS ||
        event_b == HAL_EVENT_SHORT_PRESS) {
        sm_selected = !sm_selected;
    }

    /* 第三步：长按确认按钮切换状态 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (sm_selected == 0) {
            sm_running = !sm_running;
        } else {
            sm_debug = !sm_debug;
        }
    }

    /* 第四步：长按返回按钮退出 App */
    if (event_b == HAL_EVENT_LONG_PRESS) {
        /* 避免在退出动画期间重复触发，导致状态机重置 */
        xerintosh_user_item_t* current = xerintosh_to_user_item(g_xerintosh_selector.selected_item);
        if (current != NULL && !current->exiting_user_item) {
            xerintosh_selector_exit_current_item();
        }
        return;
    }

    /* 第五步：双击滚动终端 */
    if (event_a == HAL_EVENT_DOUBLE_CLICK) {
        if (sm_buffer.scroll < sm_buffer.count - 1) {
            sm_buffer.scroll++;
        }
    }
    if (event_b == HAL_EVENT_DOUBLE_CLICK) {
        if (sm_buffer.scroll > 0) {
            sm_buffer.scroll--;
        }
    }

    /* 第六步：更新反色闪烁相位 */
    uint32_t now = hal_get_ticks();
    if (now - sm_blink_tick >= SM_BLINK_PERIOD) {
        sm_blink_tick = now;
        sm_blink_on = !sm_blink_on;
    }

    /* 第七步：绘制界面 */
    draw_info_bar();
    draw_terminal();
}

/**
 * @brief 退出串口监视器 App
 * @note  NORM 模式下清空缓冲区；DEBUG 模式下保留历史缓存
 */
void serial_monitor_exit(void)
{
    if (!sm_debug) {
        sm_buffer_clear(&sm_buffer);
    }
}

/**
 * @brief 后台串口数据读取
 * @note  供 main.cpp 的 loop() 每帧调用。
 *        仅在 START 或 DEBUG 状态下读取串口数据。
 *        若 serial_input 处于 WAITING 状态，暂停读取避免竞争。
 */
void serial_monitor_update(void)
{
    if (!sm_running && !sm_debug) return;

    /* 若 serial_input 正在等待密码/配对码，暂停读取 */
    serial_state_t st = serial_poll();
    if (st == SERIAL_STATE_WAITING_PASSWORD ||
        st == SERIAL_STATE_WAITING_PAIR_CODE) {
        return;
    }

#ifndef NATIVE_TEST
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;

        if (c == '\n' || c == '\r') {
            if (sm_rx_len > 0) {
                sm_rx_buf[sm_rx_len] = '\0';
                sm_buffer_add_line(&sm_buffer, sm_rx_buf, true);
                sm_rx_len = 0;
            }
        } else if (sm_rx_len < SM_TERM_LINE_LEN - 1) {
            sm_rx_buf[sm_rx_len++] = (char)c;
        }
    }
#endif
}
