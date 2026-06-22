/**
 * @file   serial_input.cpp
 * @brief  串口输入管理实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：通过 ESP-IDF UART 实现非阻塞输入，
 *            支持 WiFi 密码接收、退格编辑、超时自动取消及输入掩码（*）。
 *
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>

#ifdef NATIVE_TEST

#include "serial_input.h"

void serial_request_wifi_password(const char *ssid) { (void)ssid; }
void serial_cancel(void) {}
serial_state_t serial_poll(void) { return SERIAL_STATE_IDLE; }
const char* serial_get_input(void) { return NULL; }
const char* serial_get_target_name(void) { return NULL; }
bool serial_input_is_waiting(void) { return false; }

#else

#include "serial_input.h"
#include "hal/hal_system.h"
#include <string.h>
#include "driver/uart.h"

/* ═══ 常量 ═══ */

#define INPUT_BUFFER_SIZE   65   /* 64 字符 + 终止符 */
#define PASSWORD_MAX_LEN    64   /* 密码最大长度 */
#define TIMEOUT_MS          30000 /* 输入超时：30 秒 */

/* ═══ 模块状态（文件作用域）═══ */

static serial_state_t g_serial_state    = SERIAL_STATE_IDLE;   /* 当前状态 */
static char           g_input_buffer[INPUT_BUFFER_SIZE];       /* 输入缓冲区 */
static size_t         g_input_len      = 0;                    /* 当前输入长度 */
static char           g_target_name[64];                       /* 目标名称（SSID） */
static uint32_t       g_wait_start_ms  = 0;                    /* 等待开始时间 */
static bool           g_input_consumed = false;                /* 输入是否已被消费 */

/* ═══ 内部辅助函数 ═══ */

/**
 * @brief 清空输入缓冲区
 */
static void clear_buffer(void)
{
    g_input_buffer[0] = '\0';
    g_input_len       = 0;
    g_input_consumed  = false;
}

/**
 * @brief 向 UART 输出字符串
 */
static void uart_print(const char *str)
{
    uart_write_bytes(UART_NUM_0, str, strlen(str));
}

/**
 * @brief 向 UART 输出字符串并换行
 */
static void uart_println(const char *str)
{
    uart_write_bytes(UART_NUM_0, str, strlen(str));
    uart_write_bytes(UART_NUM_0, "\r\n", 2);
}

/* ═══ 公共 API ═══ */

/**
 * @brief 请求通过串口输入指定 SSID 的 WiFi 密码
 */
void serial_request_wifi_password(const char *ssid)
{
    uart_println("");
    uart_print("PASSWORD for ");
    uart_print(ssid);
    uart_print(": ");

    strlcpy(g_target_name, ssid, sizeof(g_target_name));
    clear_buffer();
    g_serial_state = SERIAL_STATE_WAITING_PASSWORD;
    g_wait_start_ms = hal_get_ticks();
}

/**
 * @brief 取消当前串口输入请求
 */
void serial_cancel(void)
{
    g_serial_state = SERIAL_STATE_CANCELLED;
    clear_buffer();
}

/**
 * @brief 轮询串口输入状态（非阻塞）
 * @return 当前状态
 * @note   处理流程：
 *         1. 若输入已被消费，自动回到 IDLE
 *         2. 检查超时（30 秒）
 *         3. 逐字节读取串口，处理回车、退格、可打印字符
 */
serial_state_t serial_poll(void)
{
    /* ─── 输入消费后自动回到 IDLE ─── */
    if (g_input_consumed &&
        g_serial_state == SERIAL_STATE_PASSWORD_RECEIVED)
    {
        g_serial_state = SERIAL_STATE_IDLE;
        return g_serial_state;
    }

    /* ─── 仅在等待密码状态下处理输入 ─── */
    if (g_serial_state != SERIAL_STATE_WAITING_PASSWORD)
    {
        return g_serial_state;
    }

    /* ─── 超时检查 ─── */
    if ((hal_get_ticks() - g_wait_start_ms) >= TIMEOUT_MS)
    {
        uart_println("\n[TIMEOUT]");
        g_serial_state = SERIAL_STATE_CANCELLED;
        clear_buffer();
        return g_serial_state;
    }

    /* ─── 逐字节读取串口（非阻塞）─── */
    while (true)
    {
        uint8_t byte;
        int n = uart_read_bytes(UART_NUM_0, &byte, 1, 0);
        if (n <= 0) {
            break;
        }

        char c = (char)byte;

        /* 回车/换行 -> 确认输入 */
        if (c == '\n' || c == '\r')
        {
            g_input_buffer[g_input_len] = '\0';
            uart_println(""); /* 回显换行 */

            g_serial_state = SERIAL_STATE_PASSWORD_RECEIVED;
            return g_serial_state;
        }

        /* 退格（BS 或 DEL） */
        if (c == '\b' || c == 0x7F)
        {
            if (g_input_len > 0)
            {
                g_input_len--;
                g_input_buffer[g_input_len] = '\0';
                /* 回显退格序列：光标后退、空格覆盖、再后退 */
                uart_print("\b \b");
            }
            continue;
        }

        /* 忽略其他控制字符 */
        if (c < 0x20) {
            continue;
        }

        /* 追加可打印字符（若缓冲区有空间） */
        if (g_input_len < PASSWORD_MAX_LEN)
        {
            g_input_buffer[g_input_len++] = c;
            g_input_buffer[g_input_len]   = '\0';
            uart_print("*"); /* 掩码显示 */
        }
    }

    return g_serial_state;
}

/**
 * @brief 获取用户输入的字符串
 * @return 输入字符串指针；状态不对时返回 NULL
 * @note   首次调用后会标记为已消费，下次调用返回 NULL
 */
const char *serial_get_input(void)
{
    if (g_serial_state == SERIAL_STATE_PASSWORD_RECEIVED)
    {
        g_input_consumed = true;
        return g_input_buffer;
    }
    return NULL;
}

/**
 * @brief 获取当前输入目标名称
 */
const char *serial_get_target_name(void)
{
    return g_target_name;
}

bool serial_input_is_waiting(void)
{
    return g_serial_state == SERIAL_STATE_WAITING_PASSWORD;
}

#endif /* NATIVE_TEST */
