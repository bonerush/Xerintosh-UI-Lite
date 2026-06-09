/**
 * @file   flasher_protocol_stk500.h
 * @brief  STK500v1 协议头文件（Arduino Optiboot 兼容）
 * @details 定义 STK500v1 协议常量、会话状态机及编程接口。
 *          用于通过 UART 对 ATmega328P 等 AVR 芯片进行 bootloader 烧录。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FLASHER_PROTOCOL_STK500_H
#define FLASHER_PROTOCOL_STK500_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ═══ STK500v1 常量（来自 AVRDUDE / optiboot stk500.h）═══ */

#define STK_OK              0x10
#define STK_FAILED          0x11
#define STK_UNKNOWN         0x12
#define STK_NODEVICE        0x13
#define STK_INSYNC          0x14
#define STK_NOSYNC          0x15
#define CRC_EOP             0x20

#define STK_GET_SYNC        0x30
#define STK_GET_SIGN_ON     0x31
#define STK_SET_PARAMETER   0x40
#define STK_GET_PARAMETER   0x41
#define STK_SET_DEVICE      0x42
#define STK_SET_DEVICE_EXT  0x45
#define STK_ENTER_PROGMODE  0x50
#define STK_LEAVE_PROGMODE  0x51
#define STK_CHIP_ERASE      0x52
#define STK_CHECK_AUTOINC   0x53
#define STK_LOAD_ADDRESS    0x55
#define STK_UNIVERSAL       0x56
#define STK_PROG_PAGE       0x64
#define STK_READ_PAGE       0x74
#define STK_READ_SIGN       0x75

#define STK500_FLASH_PAGE_SIZE 128  /**< ATmega328P Flash 页大小（字节） */
#define STK500_SYNC_TIMEOUT_MS 200  /**< SYNC 响应超时（毫秒） */
#define STK500_CMD_TIMEOUT_MS  500  /**< 命令响应超时（毫秒） */

/* ═══ STK500v1 会话状态机 ═══ */

typedef enum {
    STK500_STATE_IDLE = 0,
    STK500_STATE_CONNECTING,
    STK500_STATE_ENTER_PROGMODE,
    STK500_STATE_LOAD_ADDRESS,
    STK500_STATE_PROG_PAGE,
    STK500_STATE_LEAVE_PROGMODE,
    STK500_STATE_DONE,
    STK500_STATE_FAILED
} stk500_state_t;

typedef struct {
    stk500_state_t state;
    uint32_t total_size;     /**< 固件总大小（字节） */
    uint32_t written_size;   /**< 已写入大小（字节） */
    uint16_t current_addr;   /**< 当前字地址（字节地址 / 2） */
    int      last_error;     /**< 上次错误码（保留） */
} stk500_session_t;

/* ═══ 会话生命周期 ═══ */

/**
 * @brief 初始化 STK500v1 会话
 * @param s    会话结构体指针
 * @param size 固件总大小（字节）
 */
void stk500_session_init(stk500_session_t *s, uint32_t size);

/* ═══ 底层命令接口 ═══ */

/**
 * @brief  尝试与目标建立 STK500v1 同步
 * @note   发送 STK_GET_SYNC + CRC_EOP，等待 STK_INSYNC + STK_OK
 * @return true 同步成功
 */
bool stk500_try_sync(void);

/**
 * @brief  发送进入编程模式命令
 * @return true 成功
 */
bool stk500_enter_progmode(void);

/**
 * @brief  加载 Flash 地址（字地址）
 * @param  word_addr 字地址（字节地址需先除以 2）
 * @return true 成功
 */
bool stk500_load_address(uint16_t word_addr);

/**
 * @brief  编程一页 Flash
 * @param  data 数据缓冲区
 * @param  len  数据长度（必须 <= STK500_FLASH_PAGE_SIZE）
 * @return true 成功
 */
bool stk500_program_page(const uint8_t *data, uint16_t len);

/**
 * @brief  发送离开编程模式命令
 * @return true 成功
 */
bool stk500_leave_progmode(void);

/**
 * @brief  计算当前烧录进度
 * @param  s 会话状态
 * @return 进度 0-100
 */
int stk500_get_progress(const stk500_session_t *s);

#ifdef __cplusplus
}
#endif

#endif
