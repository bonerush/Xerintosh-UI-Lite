/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期
 * @details 实现固件烧录器的初始化、主循环、退出及蓝牙串口数据接收。
 *          支持 ESP32 ROM Bootloader SLIP 协议和 STK500v1 (Arduino Optiboot) 协议，
 *          可自动识别目标芯片类型。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
#include "flasher_protocol.h"
#include "flasher_protocol_stk500.h"
#include "flasher_ui.h"
#include <string.h>
#include "app/bluetooth/bt_manager.h"
#include "app/bluetooth/bt_uart_service.h"
#include "app/settings/settings.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_selector.h"
#include "ui/ui_item.h"

/* ═══ 状态变量 ═══ */

/* 透传阶段：确保 DTR 脉冲与 bootloader 启动期间不转发数据 */
typedef enum {
    PT_PHASE_IDLE = 0,           /* 正常双向透传 */
    PT_PHASE_DTR_WAIT,           /* 等待执行 DTR 脉冲 */
    PT_PHASE_BOOTLOADER_WAIT     /* DTR 已完成，等待目标 bootloader 就绪 */
} pt_phase_t;

static flasher_ui_state_t         s_ui;
static flasher_session_t          s_session;
static stk500_session_t           s_stk500_session;
static flasher_protocol_type_t    s_protocol_type = FLASHER_PROTO_DEFAULT;
static bool                       s_pass_through = false;       /* 透传模式开关 */
static uint32_t                   s_pt_tx_bytes = 0;            /* 透传：蓝牙→UART 字节数 */
static uint32_t                   s_pt_rx_bytes = 0;            /* 透传：UART→蓝牙 字节数 */
static pt_phase_t                 s_pt_phase = PT_PHASE_IDLE;   /* 透传阶段状态 */
static uint32_t                   s_pt_phase_until_ms = 0;      /* 阶段超时时间点 */
static bool                       s_pt_first_data = false;      /* 是否已收到首包数据 */
static bool                       s_running = false;
static bool                       s_prev_landscape = true;
static float                      s_entry_offset = 0.0f;
static bool                       s_bt_lazy_inited = false;

#define FW_BUF_SIZE 2048
static uint8_t s_fw_buf[FW_BUF_SIZE];
static int     s_fw_len = 0;

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>

/**
 * @brief 蓝牙连接状态回调（在 loop 任务 bt_uart_poll 上下文中执行）
 * @note  透传模式下，PC 一连接蓝牙就预复位目标板，让 bootloader 提前就绪。
 */
static void flasher_on_bt_connect(bool connected)
{
    if (!s_pass_through || !s_running) return;
    if (connected) {
        /* PC 已连接：标记执行 DTR 脉冲，bootloader 启动后 avrdude 的 sync 才能成功 */
        if (s_pt_phase == PT_PHASE_IDLE) {
            s_pt_phase = PT_PHASE_DTR_WAIT;
            s_pt_phase_until_ms = hal_get_ticks() + 1;
        }
    }
}

/**
 * @brief BT RX 回调（在 UI 任务 bt_uart_drain_rx_queue 上下文中执行）
 * @note  离线模式：累积到缓冲区供协议状态机消费。
 *        透传模式：直接转发到 UART，并检测 avrdude STK500 sync 序列自动复位目标。
 *        仅在烧录器运行中（s_running）时处理数据。
 */
static void flasher_on_bt_rx(const uint8_t *data, uint16_t len)
{
    if (!s_running) return;
    if (s_pass_through) {
        s_pt_tx_bytes += len;

        /* 第一次收到数据后备触发 DTR（若连接回调未触发） */
        if (!s_pt_first_data) {
            s_pt_first_data = true;
            if (s_pt_phase == PT_PHASE_IDLE) {
                s_pt_phase = PT_PHASE_DTR_WAIT;
                s_pt_phase_until_ms = hal_get_ticks() + 1;
            }
            return; /* 首包数据在目标板复位前到达，直接丢弃 */
        }

        /* DTR 脉冲或 bootloader 等待期间丢弃数据，避免发到正在复位的目标板 */
        if (s_pt_phase == PT_PHASE_IDLE) {
            flasher_uart_write(data, len);
        }
    } else {
        /* 离线烧录：累积到固件缓冲区 */
        for (uint16_t i = 0; i < len && s_fw_len < FW_BUF_SIZE; i++) {
            s_fw_buf[s_fw_len++] = data[i];
        }
    }
}
#endif

/* ═══ 协议自动识别 ═══ */

/**
 * @brief 尝试识别目标芯片使用的协议
 * @note  1. 先尝试 ESP32 ROM Bootloader（SLIP 同步）
 *        2. 失败后尝试 STK500v1（Arduino Optiboot）
 *        3. 设置 s_protocol_type 为识别到的协议
 * @return true 识别成功
 */
static bool flasher_detect_protocol(void)
{
    /* ── 尝试 ESP32 ── */
    flasher_enter_download_mode();

    uint8_t cmd_buf[64], slip_buf[128];
    uint8_t sync_data[36] = {0x07, 0x07, 0x12, 0x20};
    memset(sync_data + 4, 0x55, 32);
    int cmd_len = flasher_build_cmd(cmd_buf, 64, FLASHER_CMD_SYNC, 0, sync_data, 36);
    int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 128);
    if (slip_len > 0) {
        flasher_uart_write(slip_buf, slip_len);
    }

    hal_delay_ms(50);
    uint8_t resp[256];
    int resp_len = flasher_uart_read(resp, 256);
    if (resp_len > 0) {
        s_protocol_type = FLASHER_PROTO_ESP32;
        return true;
    }

    /* ── 尝试 STK500v1 ── */
    /* 避免 ESP32 探测将 BOOT 保持 LOW 对 Arduino 产生副作用 */
    flasher_set_boot(false); /* BOOT = HIGH */

    /* DTR 脉冲触发 Arduino 自动复位电路 */
    flasher_set_dtr(true);   /* LOW */
    hal_delay_ms(1);
    flasher_set_dtr(false);  /* HIGH */

    /* 参考 STK500.cpp：toggle_Reset() 后 delay(500) */
    hal_delay_ms(500);       /* 等待 optiboot 初始化 UART */

    if (stk500_try_sync()) {
        s_protocol_type = FLASHER_PROTO_STK500V1;
        return true;
    }

    return false;
}

/* ═══ App 生命周期 ═══ */

/**
 * @brief 初始化烧录器 App
 * @note  离线模式：重置状态、初始化协议状态机、等待用户启动。
 *        透传模式：直接开启蓝牙↔UART 双向透传，自动检测 sync 复位目标。
 */
void flasher_init(void *ud)
{
    (void)ud;
    s_pass_through = g_flasher_pass_through;
    s_fw_len = 0;
    s_bt_lazy_inited = false;
    s_entry_offset = (float)SCREEN_HEIGHT;
    s_protocol_type = FLASHER_PROTO_DEFAULT;
    s_pt_tx_bytes = 0;
    s_pt_rx_bytes = 0;
    s_pt_phase = PT_PHASE_IDLE;
    s_pt_phase_until_ms = 0;
    s_pt_first_data = false;

    flasher_ui_init(&s_ui);
    flasher_session_init(&s_session, 0x10000, 0);
    stk500_session_init(&s_stk500_session, 0);

    flasher_load_pin_config();
    /* 透传/离线均固定 115200：Optiboot 与 ESP32 ROM bootloader 均为此波特率 */
    uint32_t baud = 115200U;
    flasher_init_pins(baud);

#ifndef NATIVE_TEST
    s_prev_landscape = g_is_landscape;
    if (!g_is_landscape) {
        /* 从竖屏菜单进入时临时切换到横屏 */
        g_is_landscape = true;
        g_screen_rotation_level = ORIENTATION_LANDSCAPE;
        M5.Display.setRotation(1);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_reset_events();
    hal_input_set_double_click_enabled(false);


    if (s_pass_through) {
        /* 透传模式：立即开始运行，UI 显示绿色 PASS-THRU */
        s_running = true;
        flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
    } else {
        s_running = false;
    }

    /* 懒加载 BT：如果 BT 未启用，按需初始化 */
    bool bt_was_off = !bt_mgr_is_enabled();
    if (!bt_mgr_is_enabled()) {
        bt_mgr_enable();
        s_bt_lazy_inited = true;
    }

    /* 注册 BT 回调：必须在 bt_mgr_enable() 之后，否则 bt_uart_service_init() 会清空回调 */
    bt_uart_set_rx_callback(flasher_on_bt_rx);
    bt_uart_set_connect_callback(flasher_on_bt_connect);

    /* 透传模式：如果蓝牙进 App 前已连接，立即触发预复位 */
    if (s_pass_through && !bt_was_off && bt_uart_is_connected()) {
        if (s_pt_phase == PT_PHASE_IDLE) {
            s_pt_phase = PT_PHASE_DTR_WAIT;
            s_pt_phase_until_ms = hal_get_ticks() + 1;
        }
    }
#endif
}

#ifndef NATIVE_TEST
/**
 * @brief 透传模式界面绘制
 * @note  横屏下显示：绿色 PASS-THRU 标题、当前波特率、TX/RX 流量统计、按键提示。
 */
static void flasher_pass_through_draw(void)
{
    int16_t fh = hal_get_font_height();
    int16_t cy = fh + 4;               /* 第一行基线 */

    /* 标题 */
    const char *title = "PASS-THRU";
    int16_t tw = hal_get_string_width(title);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, cy, title, COLOR_ACCENT);

    /* 波特率 */
    char buf[32];
    snprintf(buf, sizeof(buf), "115200 baud");
    tw = hal_get_string_width(buf);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, cy + fh + 4, buf, COLOR_FG);

    /* TX / RX 流量 */
    snprintf(buf, sizeof(buf), "TX:%lu", (unsigned long)s_pt_tx_bytes);
    hal_draw_string(4, cy + 2 * (fh + 4), buf, COLOR_FG);
    snprintf(buf, sizeof(buf), "P:%d", (int)s_pt_phase);
    hal_draw_string(SCREEN_WIDTH / 2 - 8, cy + 2 * (fh + 4), buf, COLOR_FG);
    snprintf(buf, sizeof(buf), "RX:%lu", (unsigned long)s_pt_rx_bytes);
    tw = hal_get_string_width(buf);
    hal_draw_string(SCREEN_WIDTH - tw - 4, cy + 2 * (fh + 4), buf, COLOR_FG);

    /* 按键提示 */
    const char *hint = "Long-A: RST";
    tw = hal_get_string_width(hint);
    hal_draw_string((SCREEN_WIDTH - tw) / 2, SCREEN_HEIGHT - 4, hint, COLOR_FG);
}
#endif

/**
 * @brief 烧录器主循环（每帧调用）
 * @note  离线模式：长按 A 切换运行/停止，协议状态机烧录。
 *        透传模式：蓝牙↔UART 双向透传，长按 A 手动复位目标。
 */
void flasher_loop(void *ud)
{
    (void)ud;
    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 第二步：透传模式阶段状态机（DTR 脉冲 + bootloader 等待） */
#ifndef NATIVE_TEST
    if (s_pass_through) {
        switch (s_pt_phase) {
            case PT_PHASE_DTR_WAIT:
                if (hal_get_ticks() >= s_pt_phase_until_ms) {
                    flasher_set_dtr(true);  /* LOW → 经电容耦合产生复位负脉冲 */
                    hal_delay_ms(1);
                    flasher_set_dtr(false); /* HIGH */
                    s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
                    s_pt_phase_until_ms = hal_get_ticks() + 300; /* 等 bootloader */
                }
                break;
            case PT_PHASE_BOOTLOADER_WAIT:
                if (hal_get_ticks() >= s_pt_phase_until_ms) {
                    s_pt_phase = PT_PHASE_IDLE;
                }
                break;
            default:
                break;
        }
    }
#endif

    /* 第三步：长按 B 退出 App */
    if (ui_user_item_try_exit(event_b)) return;

    /* 第四步：长按 A */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        if (s_pass_through) {
            /* 透传模式：手动触发目标复位 */
            flasher_set_dtr(true);
            hal_delay_ms(1);
            flasher_set_dtr(false);
            s_pt_phase = PT_PHASE_BOOTLOADER_WAIT;
            s_pt_phase_until_ms = hal_get_ticks() + 300;
            s_pt_first_data = false;
        } else {
            /* 离线模式：切换运行/停止 */
            s_running = !s_running;
            if (s_running) {
                flasher_ui_set_status(&s_ui, FLASHER_UI_LOADING);
                s_fw_len = 0;
                s_session.state = FLASHER_STATE_CONNECTING;
                s_protocol_type = FLASHER_PROTO_DEFAULT;
            } else {
                flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                s_session.state = FLASHER_STATE_FAILED;
            }
        }
    }

    /* 第四步：入场滑入动画 */
    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

#ifndef NATIVE_TEST
    /* 第五步：透传模式 UART RX → 蓝牙 TX */
    if (s_pass_through && s_running) {
        uint8_t pt_rx_buf[64];
        int pt_rx_len = flasher_uart_read(pt_rx_buf, sizeof(pt_rx_buf));
        if (pt_rx_len > 0) {
            bt_uart_send(pt_rx_buf, (uint16_t)pt_rx_len);
            s_pt_rx_bytes += (uint32_t)pt_rx_len;
        }
    }

    /* 第六步：运行中消费蓝牙 RX 队列 */
    if (s_running) {
        bt_uart_drain_rx_queue();
    }

    /* 第七步：协议状态机（仅离线模式有效） */
    if (s_running) {
        switch (s_session.state) {
            /* ── 连接 / 自动识别 ── */
            case FLASHER_STATE_CONNECTING: {
                if (s_protocol_type == FLASHER_PROTO_AUTO) {
                    if (!flasher_detect_protocol()) {
                        /* 两种协议都识别失败 */
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                        break;
                    }
                }

                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    s_session.state = FLASHER_STATE_FLASH_BEGIN;
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    if (stk500_enter_progmode()) {
                        stk500_session_init(&s_stk500_session, 0);
                        s_session.state = FLASHER_STATE_FLASH_BEGIN;
                    } else {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                    }
                }
                break;
            }

            /* ── 开始烧录 ── */
            case FLASHER_STATE_FLASH_BEGIN: {
                /* 从 BT 接收的数据中解析固件大小（前 4 字节 LE） */
                if (s_fw_len >= 4) {
                    uint32_t fw_size = (uint32_t)s_fw_buf[0] |
                                      ((uint32_t)s_fw_buf[1] << 8) |
                                      ((uint32_t)s_fw_buf[2] << 16) |
                                      ((uint32_t)s_fw_buf[3] << 24);
                    /* 移动缓冲区 */
                    memmove(s_fw_buf, s_fw_buf + 4, s_fw_len - 4);
                    s_fw_len -= 4;

                    if (s_protocol_type == FLASHER_PROTO_ESP32) {
                        s_session.total_size = fw_size;
                        s_session.written_size = 0;

                        /* 发送 FLASH_BEGIN 命令 */
                        uint8_t cmd_buf[32], slip_buf[64];
                        uint8_t begin_data[16] = {0};
                        uint32_t num_blocks = (fw_size + FLASHER_FLASH_BLOCK_SIZE - 1) / FLASHER_FLASH_BLOCK_SIZE;
                        begin_data[0] = (uint8_t)(fw_size & 0xFF);
                        begin_data[1] = (uint8_t)((fw_size >> 8) & 0xFF);
                        begin_data[2] = (uint8_t)((fw_size >> 16) & 0xFF);
                        begin_data[3] = (uint8_t)((fw_size >> 24) & 0xFF);
                        begin_data[4] = (uint8_t)(num_blocks & 0xFF);
                        begin_data[5] = (uint8_t)((num_blocks >> 8) & 0xFF);
                        begin_data[6] = (uint8_t)(FLASHER_FLASH_BLOCK_SIZE & 0xFF);
                        begin_data[7] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 8) & 0xFF);
                        begin_data[8] = (uint8_t)((s_session.flash_addr >> 8) & 0xFF);
                        begin_data[9] = (uint8_t)((s_session.flash_addr >> 16) & 0xFF);
                        begin_data[10] = (uint8_t)((s_session.flash_addr >> 24) & 0xFF);
                        int cmd_len = flasher_build_cmd(cmd_buf, 32, FLASHER_CMD_FLASH_BEGIN, 0, begin_data, 16);
                        int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 64);
                        if (slip_len > 0) {
                            flasher_uart_write(slip_buf, slip_len);
                        }
                        delay(50);

                        s_session.state = FLASHER_STATE_FLASH_DATA;
                    } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                        s_stk500_session.total_size = fw_size;
                        s_stk500_session.written_size = 0;
                        s_stk500_session.current_addr = 0;
                        s_session.state = FLASHER_STATE_FLASH_DATA;
                    }
                }
                break;
            }

            /* ── 数据传输 ── */
            case FLASHER_STATE_FLASH_DATA: {
                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    /* ESP32：从 BT 缓冲区取 1KB 数据块发送 */
                    while (s_session.written_size < s_session.total_size &&
                           s_fw_len >= FLASHER_FLASH_BLOCK_SIZE) {
                        uint8_t block[FLASHER_FLASH_BLOCK_SIZE];
                        memcpy(block, s_fw_buf, FLASHER_FLASH_BLOCK_SIZE);
                        memmove(s_fw_buf, s_fw_buf + FLASHER_FLASH_BLOCK_SIZE, s_fw_len - FLASHER_FLASH_BLOCK_SIZE);
                        s_fw_len -= FLASHER_FLASH_BLOCK_SIZE;

                        uint32_t cs = 0xEF;
                        for (int i = 0; i < FLASHER_FLASH_BLOCK_SIZE; i++) cs ^= block[i];

                        uint8_t cmd_buf[16], slip_cmd[32];
                        uint8_t data_hdr[16] = {0};
                        data_hdr[0] = (uint8_t)(FLASHER_FLASH_BLOCK_SIZE & 0xFF);
                        data_hdr[1] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 8) & 0xFF);
                        data_hdr[2] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 16) & 0xFF);
                        data_hdr[3] = (uint8_t)((FLASHER_FLASH_BLOCK_SIZE >> 24) & 0xFF);
                        data_hdr[4] = (uint8_t)(s_session.written_size & 0xFF);
                        data_hdr[5] = (uint8_t)((s_session.written_size >> 8) & 0xFF);
                        data_hdr[6] = (uint8_t)((s_session.written_size >> 16) & 0xFF);
                        data_hdr[7] = (uint8_t)((s_session.written_size >> 24) & 0xFF);
                        int cmd_len = flasher_build_cmd(cmd_buf, 16, FLASHER_CMD_FLASH_DATA, cs, data_hdr, 16);
                        int slip_cmd_len = flasher_slip_encode(cmd_buf, cmd_len, slip_cmd, 32);
                        if (slip_cmd_len > 0) {
                            flasher_uart_write(slip_cmd, slip_cmd_len);
                        }

                        uint8_t slip_data[FLASHER_FLASH_BLOCK_SIZE + 16];
                        int slip_data_len = flasher_slip_encode(block, FLASHER_FLASH_BLOCK_SIZE, slip_data, sizeof(slip_data));
                        if (slip_data_len > 0) {
                            flasher_uart_write(slip_data, slip_data_len);
                        }

                        s_session.written_size += FLASHER_FLASH_BLOCK_SIZE;
                        flasher_ui_set_progress(&s_ui, flasher_get_progress(&s_session));
                        delay(10); /* 给目标处理时间 */
                    }
                    if (s_session.written_size >= s_session.total_size) {
                        s_session.state = FLASHER_STATE_FLASH_END;
                    }
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    /* STK500v1：从 BT 缓冲区取 128B 页发送 */
                    while (s_stk500_session.written_size < s_stk500_session.total_size &&
                           s_fw_len >= STK500_FLASH_PAGE_SIZE) {
                        uint8_t page[STK500_FLASH_PAGE_SIZE];
                        memcpy(page, s_fw_buf, STK500_FLASH_PAGE_SIZE);
                        memmove(s_fw_buf, s_fw_buf + STK500_FLASH_PAGE_SIZE, s_fw_len - STK500_FLASH_PAGE_SIZE);
                        s_fw_len -= STK500_FLASH_PAGE_SIZE;

                        uint16_t word_addr = s_stk500_session.current_addr;
                        if (!stk500_load_address(word_addr)) {
                            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                            s_session.state = FLASHER_STATE_FAILED;
                            break;
                        }

                        if (!stk500_program_page(page, STK500_FLASH_PAGE_SIZE)) {
                            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                            s_session.state = FLASHER_STATE_FAILED;
                            break;
                        }

                        s_stk500_session.current_addr += (STK500_FLASH_PAGE_SIZE / 2);
                        s_stk500_session.written_size += STK500_FLASH_PAGE_SIZE;
                        flasher_ui_set_progress(&s_ui, stk500_get_progress(&s_stk500_session));
                    }

                    /* 处理最后一页：剩余不足 128 字节时 */
                    if (s_session.state != FLASHER_STATE_FAILED &&
                        s_stk500_session.written_size < s_stk500_session.total_size &&
                        s_fw_len > 0 && s_fw_len < STK500_FLASH_PAGE_SIZE) {
                        uint8_t page[STK500_FLASH_PAGE_SIZE];
                        uint16_t remain = s_fw_len;
                        memcpy(page, s_fw_buf, remain);
                        s_fw_len = 0;

                        if (!stk500_load_address(s_stk500_session.current_addr) ||
                            !stk500_program_page(page, remain)) {
                            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                            s_session.state = FLASHER_STATE_FAILED;
                        } else {
                            s_stk500_session.written_size = s_stk500_session.total_size;
                        }
                    }

                    if (s_stk500_session.written_size >= s_stk500_session.total_size) {
                        s_session.state = FLASHER_STATE_FLASH_END;
                    }
                }
                break;
            }

            /* ── 烧录结束 ── */
            case FLASHER_STATE_FLASH_END: {
                if (s_protocol_type == FLASHER_PROTO_ESP32) {
                    uint8_t cmd_buf[16], slip_buf[32];
                    uint8_t end_data[4] = {0}; /* reboot = false */
                    int cmd_len = flasher_build_cmd(cmd_buf, 16, FLASHER_CMD_FLASH_END, 0, end_data, 4);
                    int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 32);
                    if (slip_len > 0) {
                        flasher_uart_write(slip_buf, slip_len);
                    }
                    flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
                    s_session.state = FLASHER_STATE_DONE;
                    flasher_reset_target();
                } else if (s_protocol_type == FLASHER_PROTO_STK500V1) {
                    if (stk500_leave_progmode()) {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_SUCCESS);
                        /* 触发目标复位，让 Arduino 退出 bootloader 运行用户程序 */
                        flasher_set_dtr(true);
                        hal_delay_ms(1);
                        flasher_set_dtr(false);
                        hal_delay_ms(100);
                        s_session.state = FLASHER_STATE_DONE;
                    } else {
                        flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
                        s_session.state = FLASHER_STATE_FAILED;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
#endif

    /* 第八步：绘制界面 */
    if (s_pass_through) {
#ifndef NATIVE_TEST
        flasher_pass_through_draw();
#endif
    } else {
        flasher_ui_draw(&s_ui);
    }
}

/**
 * @brief 退出烧录器 App
 * @note  停止运行、注销 BT 回调、恢复屏幕方向。
 */
void flasher_exit(void *ud)
{
    (void)ud;
    s_running = false;

#ifndef NATIVE_TEST
    /* 注销 BT 回调 */
    bt_uart_set_rx_callback(NULL);
    bt_uart_set_connect_callback(NULL);

    /* 如果 BT 是由烧录器懒加载的，退出时释放以归还内存给 WiFi */
    if (s_bt_lazy_inited && bt_mgr_is_enabled()) {
        bt_mgr_disable();
        s_bt_lazy_inited = false;
    }

    if (!s_prev_landscape) {
        /* 恢复之前的竖屏方向 */
        g_is_landscape = false;
        g_screen_rotation_level = ORIENTATION_PORTRAIT;
        M5.Display.setRotation(0);
        g_screen_width = M5.Display.width();
        g_screen_height = M5.Display.height();
        hal_display_init();
    }
    hal_input_set_double_click_enabled(false);
    hal_input_reset_events();
#endif
}
