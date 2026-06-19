# 串口输入模块（Serial Input）

> **Parent:** [App 层索引](index.md) | **Related:** [WiFi 管理器](wifi.md), [蓝牙管理器](bluetooth.md), [串口监视器](serial-monitor.md)

## 概述

`serial_input` 提供通过硬件串口接收 WiFi 密码和蓝牙配对码的非阻塞状态机。它支持回车确认、退格编辑、超时自动取消以及输入掩码（`*`）显示，使设备在没有键盘的情况下仍可通过 USB 串口完成配网或配对。

## 关键概念

### 状态机

*📄 Source: [serial_input.h](../../src/app/serial_input/serial_input.h#L25-L32)*

```c
typedef enum {
    SERIAL_STATE_IDLE,                 /* 空闲 */
    SERIAL_STATE_WAITING_PASSWORD,     /* 等待输入 WiFi 密码 */
    SERIAL_STATE_PASSWORD_RECEIVED,    /* WiFi 密码已接收 */
    SERIAL_STATE_WAITING_PAIR_CODE,    /* 等待输入蓝牙配对码 */
    SERIAL_STATE_PAIR_CODE_RECEIVED,   /* 配对码已接收 */
    SERIAL_STATE_CANCELLED             /* 输入已取消/超时 */
} serial_state_t;
```

### 请求输入

*📄 Source: [serial_input.cpp](../../src/app/serial_input/serial_input.cpp#L88-L97)*

```c
void serial_request_wifi_password(const char *ssid)
{
    Serial.println();
    Serial.print("PASSWORD for ");
    Serial.print(ssid);
    Serial.print(": ");
    Serial.flush();

    enter_waiting_state(SERIAL_STATE_WAITING_PASSWORD, ssid);
}
```

调用后模块进入等待状态，向串口打印提示，随后 `serial_poll()` 每帧读取并处理字符。

### 轮询处理

*📄 Source: [serial_input.cpp](../../src/app/serial_input/serial_input.cpp#L116-L193)*

```c
serial_state_t serial_poll(void)
{
    /* 输入消费后自动回到 IDLE */
    if (g_input_consumed &&
        (g_serial_state == SERIAL_STATE_PASSWORD_RECEIVED ||
         g_serial_state == SERIAL_STATE_PAIR_CODE_RECEIVED))
    {
        g_serial_state = SERIAL_STATE_IDLE;
        return g_serial_state;
    }

    if (g_serial_state != SERIAL_STATE_WAITING_PASSWORD &&
        g_serial_state != SERIAL_STATE_WAITING_PAIR_CODE)
    {
        return g_serial_state;
    }

    /* 超时检查 */
    if ((millis() - g_wait_start_ms) >= TIMEOUT_MS)
    {
        Serial.println("\n[TIMEOUT]");
        g_serial_state = SERIAL_STATE_CANCELLED;
        clear_buffer();
        return g_serial_state;
    }

    while (Serial.available() > 0)
    {
        int raw = Serial.read();
        if (raw < 0) break;

        char c = (char)raw;

        /* 回车/换行 -> 确认输入 */
        if (c == '\n' || c == '\r')
        {
            g_input_buffer[g_input_len] = '\0';
            Serial.println();
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
                Serial.print("\b \b");
            }
            continue;
        }

        /* 忽略其他控制字符 */
        if (c < 0x20) continue;

        /* 追加可打印字符（若缓冲区有空间） */
        if (g_input_len < current_max_len())
        {
            g_input_buffer[g_input_len++] = c;
            g_input_buffer[g_input_len]   = '\0';
            Serial.print('*');
        }
    }

    return g_serial_state;
}
```

处理逻辑：
1. 已消费的结果状态会在首次轮询后自动回到 `IDLE`
2. 超过 30 秒未输入则转为 `CANCELLED`
3. 回车/换行确认输入
4. 退格键支持 `\b` 与 `0x7F`
5. 可打印字符追加到缓冲区，回显 `*` 掩码

### 获取结果

*📄 Source: [serial_input.cpp](../../src/app/serial_input/serial_input.cpp#L200-L209)*

```c
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
```

首次调用返回输入字符串并标记为已消费；后续调用返回 `NULL`。

### 等待状态查询

*📄 Source: [serial_input.cpp](../../src/app/serial_input/serial_input.cpp#L219-L223)*

```c
bool serial_input_is_waiting(void)
{
    return (g_serial_state == SERIAL_STATE_WAITING_PASSWORD ||
            g_serial_state == SERIAL_STATE_WAITING_PAIR_CODE);
}
```

`serial_input_is_waiting()` 供 `dev_ttyS0_poll()` 和 `serial_monitor_update()` 使用：当模块正在等待输入时，硬件串口字符由 `serial_input` 消费，避免被 Shell 或串口监视器截断。

## 公共 API

*📄 Source: [serial_input.h](../../src/app/serial_input/serial_input.h#L34-L73)*

| 函数 | 说明 |
|------|------|
| `serial_request_wifi_password(ssid)` | 请求输入指定 SSID 的密码 |
| `serial_cancel()` | 取消当前输入请求 |
| `serial_poll()` | 非阻塞轮询，返回当前状态 |
| `serial_get_input()` | 获取已确认的输入字符串 |
| `serial_get_target_name()` | 获取当前输入目标（SSID/设备名） |
| `serial_input_is_waiting()` | 是否正在等待输入 |

---

> **See Also:** [WiFi 管理器](wifi.md) | [蓝牙管理器](bluetooth.md) | [串口监视器](serial-monitor.md)
