/**
 * @file   ble_serial.cpp
 * @brief  BLE 串口监视器 App 实现
 * @details 通过 BLE UART（NUS）实现无线串口通信。
 *          三段式布局：Header（标题）+ Body（数据区）+ Footer（状态栏）
 *          支持入场动画、BtnA 滚动、连接状态变化弹窗提示。
 *
 * @copyright Copyright (c) 2026
 */

#include "ble_serial.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_layout.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>
#include <string.h>

/* ═══ 常量 ═══ */

#define BLE_LINE_MAX     16   /* 显示行环形缓冲区大小 */
#define BLE_LINE_LEN     48   /* 每行最大字符数（含 null） */

/* ═══ 环形缓冲区 ═══ */

typedef struct {
    char    lines[BLE_LINE_MAX][BLE_LINE_LEN];
    uint8_t head;     /* 下一行写入位置 */
    uint8_t count;    /* 有效行数 */
    int16_t scroll;   /* 滚动偏移（0 = 最新） */
} ble_line_buf_t;

static void ble_buf_init(ble_line_buf_t *b)
{
    memset(b, 0, sizeof(*b));
}

static void ble_buf_add(ble_line_buf_t *b, const char *text)
{
    if (!b || !text) return;
    strncpy(b->lines[b->head], text, BLE_LINE_LEN - 1);
    b->lines[b->head][BLE_LINE_LEN - 1] = '\0';
    b->head = (b->head + 1) % BLE_LINE_MAX;
    if (b->count < BLE_LINE_MAX) b->count++;
    b->scroll = 0; /* 新数据时重置滚动 */
}

static const char *ble_buf_get(const ble_line_buf_t *b, int16_t offset)
{
    if (!b || offset < 0 || offset >= b->count) return "";
    int16_t idx = (b->head - 1 - offset - b->scroll + BLE_LINE_MAX * 2) % BLE_LINE_MAX;
    return b->lines[idx];
}

/* ═══ 状态变量 ═══ */

static bool           s_connected = false;
static bool           s_prev_connected = false;
static uint32_t       s_tx_count  = 0;
static uint32_t       s_rx_count  = 0;
static ble_line_buf_t s_lines;

/* 入场动画 */
static float s_entry_offset = 0.0f;

/* 行组装缓冲区（RX 回调写入，loop 不触碰） */
static char     s_assembly[BLE_LINE_LEN];
static uint16_t s_asm_pos = 0;

/* ═══ BLE UART 回调 ═══ */

#ifndef NATIVE_TEST
#include "app/bluetooth/bt_uart_service.h"

static void on_ble_rx(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];
        s_rx_count++;
        if (c == '\n' || c == '\r') {
            if (s_asm_pos > 0) {
                s_assembly[s_asm_pos] = '\0';
                ble_buf_add(&s_lines, s_assembly);
                s_asm_pos = 0;
            }
        } else if (s_asm_pos < BLE_LINE_LEN - 1) {
            s_assembly[s_asm_pos++] = c;
        }
    }
}

static void on_ble_connect(bool connected)
{
    s_connected = connected;
}

#endif /* !NATIVE_TEST */

/* ═══ 绘制 ═══ */

/**
 * @brief 绘制 Header（标题栏）
 */
static void draw_header(void)
{
    int16_t entry = (s_entry_offset < 1.0f) ? 0 : (int16_t)s_entry_offset;

    xerintosh_set_font(NULL);
    int16_t title_w = hal_get_string_width("BLE Serial");
    int16_t title_x = HAL_CENTER_X(title_w);
    int16_t title_y = HAL_TEXT_BASELINE(HAL_MARGIN_SM) + entry;

    hal_draw_string(title_x, title_y, "BLE Serial", COLOR_FG);

    /* 分隔线 */
    hal_draw_h_line(0, HAL_HEADER_BOTTOM() + entry, SCREEN_WIDTH, COLOR_FG);

    /* 恢复中文主字体，同步 xerintosh_set_font 缓存 */
    xerintosh_set_font(hal_get_cn_font());
}

/**
 * @brief 绘制 Footer（状态栏）
 */
static void draw_footer(void)
{
    int16_t font_h = hal_get_font_height();
    int16_t entry = (s_entry_offset < 1.0f) ? 0 : (int16_t)s_entry_offset;
    /* 底部栏上移 2px，避免过于贴近屏幕底边 */
    int16_t footer_y = HAL_FOOTER_TOP() - 2 + entry;

    /* 分隔线 */
    hal_draw_h_line(0, footer_y, SCREEN_WIDTH, COLOR_FG);

    /* 状态 + 统计合并为一行 */
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "%s  TX:%lu RX:%lu",
             s_connected ? "Conn" : "None",
             (unsigned long)s_tx_count, (unsigned long)s_rx_count);

    int16_t status_w = hal_get_string_width(status_str);
    int16_t status_x = HAL_CENTER_X(status_w);
    /* 文字在底部栏内垂直居中 */
    int16_t status_y = footer_y + (HAL_ROW_H() + font_h) / 2;

    /* 连接状态用强调色 */
    uint16_t status_color = s_connected ? COLOR_ACCENT : COLOR_FG;
    hal_draw_string(status_x, status_y, status_str, status_color);
}

/**
 * @brief 绘制 Body（数据接收区域）
 */
static void draw_body(void)
{
    int16_t font_h = hal_get_font_height();
    int16_t entry = (s_entry_offset < 1.0f) ? 0 : (int16_t)s_entry_offset;

    int16_t body_top = HAL_HEADER_BOTTOM() + HAL_MARGIN_SM + entry;
    int16_t body_h = HAL_BODY_HEIGHT() - HAL_MARGIN_SM * 2;

    /* 裁剪区域，防止数据溢出到 Header/Footer */
    hal_set_clip_rect(0, body_top, SCREEN_WIDTH, body_h);

    int16_t max_lines = body_h / font_h;
    if (max_lines < 1) max_lines = 1;
    if (max_lines > BLE_LINE_MAX) max_lines = BLE_LINE_MAX;

    /* 绘制数据行（最新在上） */
    for (int16_t i = 0; i < max_lines && i < (int16_t)s_lines.count; i++) {
        const char *line = ble_buf_get(&s_lines, i);
        if (line[0] != '\0') {
            int16_t line_y = body_top + (i + 1) * font_h;
            /* RX 数据用强调色 */
            hal_draw_string(HAL_LEFT_X(), line_y, line, COLOR_ACCENT);
        }
    }

    /* 无数据时显示提示 */
    if (s_lines.count == 0) {
        const char *hint = "Waiting for data...";
        int16_t hint_w = hal_get_string_width(hint);
        int16_t hint_x = HAL_CENTER_X(hint_w);
        int16_t hint_y = body_top + body_h / 2 + font_h / 2;
        hal_draw_string(hint_x, hint_y, hint, COLOR_FG);
    }

    hal_clear_clip_rect();
}

/**
 * @brief 主绘制函数
 */
static void ble_serial_draw(void)
{
    draw_header();
    draw_body();
    draw_footer();
}

/* ═══ 生命周期（extern "C"） ═══ */

void ble_serial_init(void *ud)
{
    (void)ud;
    s_connected = false;
    s_prev_connected = false;
    s_tx_count  = 0;
    s_rx_count  = 0;
    s_asm_pos   = 0;
    s_entry_offset = (float)SCREEN_HEIGHT;
    ble_buf_init(&s_lines);

#ifndef NATIVE_TEST
    hal_input_reset_events();
    bt_uart_service_init();
    bt_uart_set_rx_callback(on_ble_rx);
    bt_uart_set_connect_callback(on_ble_connect);
#endif
}

void ble_serial_loop(void *ud)
{
    (void)ud;

    /* 入场动画 */
    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

#ifndef NATIVE_TEST
    s_connected = bt_uart_is_connected();
#endif

    /* 连接状态变化时弹窗提示 */
    if (s_connected != s_prev_connected) {
        xerintosh_push_pop_up(s_connected ? "Connected" : "Disconnected", 1500);
        s_prev_connected = s_connected;
    }

    /* 按键处理 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (ui_user_item_try_exit(event_b)) return;

    /* BtnA 短按：滚动数据 */
    if (event_a == HAL_EVENT_SHORT_PRESS) {
        if (s_lines.scroll < (int16_t)s_lines.count - 1) {
            s_lines.scroll++;
        }
    }

    /* BtnA 双击：重置滚动 */
    if (event_a == HAL_EVENT_DOUBLE_CLICK) {
        s_lines.scroll = 0;
    }

    ble_serial_draw();
}

void ble_serial_exit(void *ud)
{
    (void)ud;
    ble_buf_init(&s_lines);
    s_asm_pos = 0;

#ifndef NATIVE_TEST
    bt_uart_set_rx_callback(NULL);
    bt_uart_set_connect_callback(NULL);
    bt_uart_service_deinit();
    hal_input_reset_events();
#endif
}
