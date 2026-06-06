/**
 * @file   flasher_app.cpp
 * @brief  烧录器 user_item App 生命周期
 * @details 实现固件烧录器的初始化、主循环、退出及蓝牙串口数据接收。
 *          基于已实现的 GPIO/Protocol/UI 模块，提供用户可交互的烧录界面。
 *
 * @copyright Copyright (c) 2026
 */

#include "flasher.h"
#include "flasher_gpio.h"
#include "flasher_protocol.h"
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

static flasher_ui_state_t    s_ui;
static flasher_session_t     s_session;
static bool                  s_running = false;
static bool                  s_prev_landscape = true;
static float                 s_entry_offset = 0.0f;
static bool                  s_bt_lazy_inited = false;

#define FW_BUF_SIZE 2048
static uint8_t s_fw_buf[FW_BUF_SIZE];
static int     s_fw_len = 0;

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <M5Unified.h>

/**
 * @brief BT RX 回调（在 UI 任务 bt_uart_drain_rx_queue 上下文中执行）
 * @note  将接收到的固件数据累积到缓冲区，供协议状态机消费。
 *        仅在烧录器运行中（s_running）时处理数据。
 */
static void flasher_on_bt_rx(const uint8_t *data, uint16_t len)
{
    if (!s_running) return;
    for (uint16_t i = 0; i < len && s_fw_len < FW_BUF_SIZE; i++) {
        s_fw_buf[s_fw_len++] = data[i];
    }
}
#endif

/**
 * @brief 初始化烧录器 App
 * @note  重置所有状态、初始化 UI 与会话、设置横屏方向并注册蓝牙回调。
 */
void flasher_init(void *ud)
{
    (void)ud;
    s_running = false;
    s_fw_len = 0;
    s_bt_lazy_inited = false;
    s_entry_offset = (float)SCREEN_HEIGHT;

    flasher_ui_init(&s_ui);
    flasher_session_init(&s_session, 0x10000, 0);

    flasher_load_pin_config();
    flasher_init_pins();

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

    /* 注册 BT 回调 */
    bt_uart_set_rx_callback(flasher_on_bt_rx);

    /* 懒加载 BT：如果 BT 未启用，按需初始化 */
    if (!bt_mgr_is_enabled()) {
        bt_mgr_enable();
        s_bt_lazy_inited = true;
    }
#endif
}

/**
 * @brief 烧录器主循环（每帧调用）
 * @note  处理按键事件、更新动画、绘制进度条 UI。
 *        长按 A 切换运行/停止状态，长按 B 退出 App。
 */
void flasher_loop(void *ud)
{
    (void)ud;
    /* 第一步：读取按键事件 */
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 第二步：长按 B 退出 App */
    if (ui_user_item_try_exit(event_b)) return;

    /* 第三步：长按 A 切换运行状态 */
    if (event_a == HAL_EVENT_LONG_PRESS) {
        s_running = !s_running;
        if (s_running) {
            flasher_ui_set_status(&s_ui, FLASHER_UI_LOADING);
            s_fw_len = 0;
            s_session.state = FLASHER_STATE_CONNECTING;
        } else {
            flasher_ui_set_status(&s_ui, FLASHER_UI_FAILED);
            s_session.state = FLASHER_STATE_FAILED;
        }
    }

    /* 第四步：入场滑入动画 */
    xerintosh_animation(&s_entry_offset, 0.0f, ANIM_SPEED_EXIT);

#ifndef NATIVE_TEST
    /* 第五步：运行中消费蓝牙 RX 队列 */
    if (s_running) {
        bt_uart_drain_rx_queue();
    }

    /* 第六步：协议状态机 */
    if (s_running) {
        switch (s_session.state) {
            case FLASHER_STATE_CONNECTING: {
                flasher_enter_download_mode();
                /* 发送 SYNC 命令 */
                uint8_t cmd_buf[64], slip_buf[128];
                uint8_t sync_data[36] = {0x07, 0x07, 0x12, 0x20};
                memset(sync_data + 4, 0x55, 32);
                int cmd_len = flasher_build_cmd(cmd_buf, 64, FLASHER_CMD_SYNC, 0, sync_data, 36);
                int slip_len = flasher_slip_encode(cmd_buf, cmd_len, slip_buf, 128);
                if (slip_len > 0) {
                    flasher_uart_write(slip_buf, slip_len);
                }
                /* 等待一小段时间让目标响应 */
                delay(50);
                /* 读取响应（简化处理：只要收到任何数据就算成功） */
                uint8_t resp[256];
                int resp_len = flasher_uart_read(resp, 256);
                if (resp_len > 0) {
                    s_session.state = FLASHER_STATE_FLASH_BEGIN;
                }
                break;
            }

            case FLASHER_STATE_FLASH_BEGIN: {
                /* 从 BT 接收的数据中解析固件大小（前 4 字节 LE） */
                if (s_fw_len >= 4) {
                    s_session.total_size = (uint32_t)s_fw_buf[0] |
                                          ((uint32_t)s_fw_buf[1] << 8) |
                                          ((uint32_t)s_fw_buf[2] << 16) |
                                          ((uint32_t)s_fw_buf[3] << 24);
                    /* 移动缓冲区 */
                    memmove(s_fw_buf, s_fw_buf + 4, s_fw_len - 4);
                    s_fw_len -= 4;
                    s_session.state = FLASHER_STATE_FLASH_DATA;

                    /* 发送 FLASH_BEGIN 命令 */
                    uint8_t cmd_buf[32], slip_buf[64];
                    uint8_t begin_data[16] = {0};
                    uint32_t num_blocks = (s_session.total_size + FLASHER_FLASH_BLOCK_SIZE - 1) / FLASHER_FLASH_BLOCK_SIZE;
                    begin_data[0] = (uint8_t)(s_session.total_size & 0xFF);
                    begin_data[1] = (uint8_t)((s_session.total_size >> 8) & 0xFF);
                    begin_data[2] = (uint8_t)((s_session.total_size >> 16) & 0xFF);
                    begin_data[3] = (uint8_t)((s_session.total_size >> 24) & 0xFF);
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
                }
                break;
            }

            case FLASHER_STATE_FLASH_DATA: {
                /* 从 BT 缓冲区取 1KB 数据块发送 */
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
                break;
            }

            case FLASHER_STATE_FLASH_END: {
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
                break;
            }

            default:
                break;
        }
    }
#endif

    /* 第七步：绘制界面 */
    flasher_ui_draw(&s_ui);
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
