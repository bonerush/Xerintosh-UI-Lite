/**
 * @file   serial_input.cpp
 * @brief  串口输入管理实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：所有函数为空桩
 *          - 硬件环境时：通过 Arduino Serial 实现非阻塞输入，
 *            支持密码/配对码接收、退格编辑、超时自动取消及输入掩码（*）。
 *
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>

#ifdef NATIVE_TEST

#include "serial_input.h"

void serial_request_wifi_password(const char *ssid) { (void)ssid; }
void serial_request_bt_pair_code(const char *device_name) { (void)device_name; }
void serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr) {
    (void)device_name; (void)device_addr;
}
void serial_cancel(void) {}
serial_state_t serial_poll(void) { return SERIAL_STATE_IDLE; }
const char* serial_get_input(void) { return NULL; }
const char* serial_get_target_name(void) { return NULL; }
const char* serial_get_target_addr(void) { return NULL; }
bool serial_input_is_waiting(void) { return false; }

#else

#include "serial_input.h"
#include <Arduino.h>
#include <string.h>

/* ═══ 常量 ═══ */

#define INPUT_BUFFER_SIZE   65   /* 64 字符 + 终止符 */
#define PASSWORD_MAX_LEN    64   /* 密码最大长度 */
#define PAIR_CODE_MAX_LEN   16   /* 配对码最大长度 */
#define TIMEOUT_MS          30000 /* 输入超时：30 秒 */

/* ═══ 模块状态（文件作用域）═══ */

static serial_state_t g_serial_state    = SERIAL_STATE_IDLE;   /* 当前状态 */
static char           g_input_buffer[INPUT_BUFFER_SIZE];       /* 输入缓冲区 */
static size_t         g_input_len      = 0;                    /* 当前输入长度 */
static char           g_target_name[64];                       /* 目标名称（SSID 或设备名） */
static char           g_target_addr[18];                       /* 蓝牙 MAC 地址 */
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
 * @brief 获取当前等待状态下的最大输入长度
 */
static size_t current_max_len(void)
{
    return (g_serial_state == SERIAL_STATE_WAITING_PASSWORD) ? PASSWORD_MAX_LEN
                                                    : PAIR_CODE_MAX_LEN;
}

/**
 * @brief 进入等待输入状态
 * @param waiting_state 等待的状态类型
 * @param name          目标名称
 */
static void enter_waiting_state(serial_state_t waiting_state,
                                const char *name)
{
    strlcpy(g_target_name, name, sizeof(g_target_name));
    clear_buffer();
    g_serial_state = waiting_state;
    g_wait_start_ms = millis();
}

/* ═══ 公共 API ═══ */

/**
 * @brief 请求通过串口输入指定 SSID 的 WiFi 密码
 */
void serial_request_wifi_password(const char *ssid)
{
    Serial.println();
    Serial.print("PASSWORD for ");
    Serial.print(ssid);
    Serial.print(": ");
    Serial.flush();

    enter_waiting_state(SERIAL_STATE_WAITING_PASSWORD, ssid);
}

/**
 * @brief 请求通过串口输入指定蓝牙设备的配对码
 */
void serial_request_bt_pair_code(const char *device_name)
{
    Serial.print("PAIR CODE for ");
    Serial.print(device_name);
    Serial.print(": ");
    Serial.flush();

    g_target_addr[0] = '\0';
    enter_waiting_state(SERIAL_STATE_WAITING_PAIR_CODE, device_name);
}

/**
 * @brief 请求通过串口输入指定蓝牙设备的配对码（含 MAC 地址）
 */
void serial_request_bt_pair_code_with_addr(const char *device_name, const char *device_addr)
{
    Serial.print("PAIR CODE for ");
    Serial.print(device_name);
    Serial.print(" [");
    Serial.print(device_addr);
    Serial.print("]: ");
    Serial.flush();

    strlcpy(g_target_addr, device_addr ? device_addr : "", sizeof(g_target_addr));
    enter_waiting_state(SERIAL_STATE_WAITING_PAIR_CODE, device_name);
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
        (g_serial_state == SERIAL_STATE_PASSWORD_RECEIVED ||
         g_serial_state == SERIAL_STATE_PAIR_CODE_RECEIVED))
    {
        g_serial_state = SERIAL_STATE_IDLE;
        return g_serial_state;
    }

    /* ─── 仅在等待状态下处理输入 ─── */
    if (g_serial_state != SERIAL_STATE_WAITING_PASSWORD &&
        g_serial_state != SERIAL_STATE_WAITING_PAIR_CODE)
    {
        return g_serial_state;
    }

    /* ─── 超时检查 ─── */
    if ((millis() - g_wait_start_ms) >= TIMEOUT_MS)
    {
        Serial.println("\n[TIMEOUT]");
        g_serial_state = SERIAL_STATE_CANCELLED;
        clear_buffer();
        return g_serial_state;
    }

    /* ─── 逐字节读取串口（非阻塞）─── */
    while (Serial.available() > 0)
    {
        int raw = Serial.read();
        if (raw < 0) {
            break;
        }

        char c = (char)raw;

        /* 回车/换行 -> 确认输入 */
        if (c == '\n' || c == '\r')
        {
            g_input_buffer[g_input_len] = '\0';
            Serial.println(); /* 回显换行 */

            g_serial_state = (g_serial_state == SERIAL_STATE_WAITING_PASSWORD)
                        ? SERIAL_STATE_PASSWORD_RECEIVED
                        : SERIAL_STATE_PAIR_CODE_RECEIVED;
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
                Serial.print("\b \b");
            }
            continue;
        }

        /* 忽略其他控制字符 */
        if (c < 0x20) {
            continue;
        }

        /* 追加可打印字符（若缓冲区有空间） */
        if (g_input_len < current_max_len())
        {
            g_input_buffer[g_input_len++] = c;
            g_input_buffer[g_input_len]   = '\0';
            Serial.print('*'); /* 掩码显示 */
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
    if (g_serial_state == SERIAL_STATE_PASSWORD_RECEIVED ||
        g_serial_state == SERIAL_STATE_PAIR_CODE_RECEIVED)
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

/**
 * @brief 获取当前输入目标 MAC 地址
 */
const char *serial_get_target_addr(void)
{
    return g_target_addr[0] ? g_target_addr : NULL;
}

bool serial_input_is_waiting(void)
{
    return (g_serial_state == SERIAL_STATE_WAITING_PASSWORD ||
            g_serial_state == SERIAL_STATE_WAITING_PAIR_CODE);
}

#endif /* NATIVE_TEST */
