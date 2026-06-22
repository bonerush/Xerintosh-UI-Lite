/**
 * @file   app_input.c
 * @brief  App 每帧输入处理实现
 * @details 将硬件按键事件映射到 UI 选择器导航，
 *          并调度各模块状态机（电源键弹窗、WiFi 弹窗、烧录器强制解除等）。
 *
 * @copyright Copyright (c) 2026
 */

#include "app_input.h"

#include "app_state.h"
#include "settings/settings.h"
#include "wifi/wifi_manager.h"
#include "serial_input/serial_input.h"
#include "app/shutdown/power_key_popup.h"
#include "app/flasher/flasher_menu.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"
#include "ui/ui_drawer.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"

/* WiFi 弹窗刷新（定义在 wifi_manager.cpp，UI 任务每帧调用） */
extern void wifi_popup_refresh(void);

/**
 * @brief 处理按键输入事件，映射到 UI 选择器操作
 * @note  按键映射：
 *        - Btn B 短按：选择器上移（上一项）
 *        - Btn B 长按：退出当前项 / 取消输入
 *        - Btn A 短按：选择器下移（下一项）
 *        - Btn A 长按：确认/进入当前项
 */
void app_input_process(void)
{
    /* 无论处于何种模式，每帧都必须调用 hal_input_update()，
       确保 M5.update() 刷新按键边沿标志。
       user_item 内部虽然不处理框架导航，但 user_item 自身
       的 loop 会调用 hal_input_get_event() 读取按键。 */
    hal_input_update();

    /* 更新电源键弹窗（检测 A+B 双键关机事件） */
    power_key_popup_update();

    /* 刷新 WiFi 弹窗（跨任务弹窗，每帧 push 以保持显示） */
    wifi_popup_refresh();

    /* 烧录器引脚菜单：延迟弹窗 + 强制解除状态机 */
    flasher_menu_process_input();

    /* 双键按住模式下，隔离所有正常按钮事件，防止 UI 抖动 */
    if (power_key_popup_is_dual_active()) {
        return;
    }

    /* 若处于 user_item 内部，框架输入由 App 自身接管 */
    if (xerintosh_is_in_user_item()) {
        return;
    }

    /* 进/退场动画期间禁止框架输入，避免误触发 */
    if (!g_xerintosh_exit_animation_finished) {
        return;
    }

    /* 烧录器强制解除状态机激活时，跳过框架导航 */
    if (flasher_menu_is_active()) {
        return;
    }

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_b == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_prev_item();
    }
    else if (event_b == HAL_EVENT_LONG_PRESS)
    {
        /* 若 WiFi 正在等待串口输入，先取消输入 */
        if (wifi_mgr_is_waiting_input()) {
            serial_cancel();
        }
        xerintosh_selector_exit_current_item();
    }

    if (event_a == HAL_EVENT_SHORT_PRESS)
    {
        xerintosh_selector_go_next_item();
    }
    else if (event_a == HAL_EVENT_LONG_PRESS)
    {
        xerintosh_selector_jump_to_selected_item();
    }
}
