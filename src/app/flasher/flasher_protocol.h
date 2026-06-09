/**
 * @file   flasher_protocol.h
 * @brief  烧录器协议抽象层头文件
 * @details 定义协议类型枚举、ESP32 ROM Bootloader SLIP 协议状态机、
 *          SLIP 编解码、命令包构建及会话管理接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef FLASHER_PROTOCOL_H
#define FLASHER_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ═══ 协议类型枚举（用于自动识别和多协议支持）═══ */

typedef enum {
    FLASHER_PROTO_AUTO = 0,   /**< 自动识别 */
    FLASHER_PROTO_ESP32,      /**< ESP32 ROM Bootloader SLIP 协议 */
    FLASHER_PROTO_STK500V1,   /**< STK500v1 (Arduino Optiboot) */
    FLASHER_PROTO_COUNT
} flasher_protocol_type_t;

#define FLASHER_PROTO_DEFAULT FLASHER_PROTO_AUTO

/* ═══ ESP32 SLIP 协议状态机 ═══ */

typedef enum {
    FLASHER_STATE_IDLE = 0,
    FLASHER_STATE_CONNECTING,
    FLASHER_STATE_FLASH_BEGIN,
    FLASHER_STATE_FLASH_DATA,
    FLASHER_STATE_FLASH_END,
    FLASHER_STATE_VERIFY,
    FLASHER_STATE_DONE,
    FLASHER_STATE_FAILED
} flasher_state_t;

typedef enum {
    FLASHER_CMD_SYNC          = 0x08,
    FLASHER_CMD_READ_REG      = 0x0A,
    FLASHER_CMD_FLASH_BEGIN   = 0xD0,
    FLASHER_CMD_FLASH_DATA    = 0xD2,
    FLASHER_CMD_FLASH_END     = 0xD4,
    FLASHER_CMD_SPI_FLASH_MD5 = 0x13,
} flasher_cmd_t;

#define FLASHER_FLASH_BLOCK_SIZE 0x400  /* 1KB blocks */
#define FLASHER_SYNC_TIMEOUT_MS  2000
#define FLASHER_CMD_TIMEOUT_MS   5000

typedef struct {
    flasher_state_t state;
    uint32_t total_size;
    uint32_t written_size;
    uint32_t flash_addr;     /* typically 0x10000 for app partition */
    uint32_t chip_id;
    int      last_error;
} flasher_session_t;

/**
 * @brief 初始化协议会话
 * @param s    会话结构体指针
 * @param addr 目标 Flash 地址（如 0x10000）
 * @param size 固件总大小（字节）
 */
void flasher_session_init(flasher_session_t *s, uint32_t addr, uint32_t size);

/**
 * @brief  SLIP 编码：将原始数据包装为 SLIP 帧
 * @param  in      原始数据
 * @param  in_len  原始数据长度
 * @param  out     输出缓冲区
 * @param  out_max 输出缓冲区最大长度
 * @return 编码后的帧长度；失败返回 -1
 * @note   输出格式：[0xC0][escaped data][0xC0]
 *         转义规则：0xC0 -> 0xDB 0xDC, 0xDB -> 0xDB 0xDD
 */
int flasher_slip_encode(const uint8_t *in, int in_len, uint8_t *out, int out_max);

/**
 * @brief  SLIP 解码：从 SLIP 帧中提取原始数据
 * @param  in      包含 SLIP 帧的缓冲区
 * @param  in_len  输入长度
 * @param  out     输出缓冲区
 * @param  out_max 输出缓冲区最大长度
 * @return 解码后的数据长度；未找到完整帧返回 -1
 */
int flasher_slip_decode(const uint8_t *in, int in_len, uint8_t *out, int out_max);

/**
 * @brief  构建 ESP32 ROM bootloader 命令包（不含 SLIP 包装）
 * @param  buf      输出缓冲区
 * @param  buf_max  缓冲区最大长度
 * @param  cmd      命令字节
 * @param  check_sum 数据校验和
 * @param  data     命令数据（可为 NULL）
 * @param  data_len 数据长度
 * @return 包长度（8 + data_len）；失败返回 -1
 * @note   包格式：[0x00][cmd][data_len_lo][data_len_hi][cs0][cs1][cs2][cs3][data...]
 */
int flasher_build_cmd(uint8_t *buf, int buf_max,
                      uint8_t cmd, uint32_t check_sum,
                      const uint8_t *data, uint16_t data_len);

/**
 * @brief  处理接收到的数据（协议状态机）
 * @param  s    会话状态
 * @param  data 接收到的数据
 * @param  len  数据长度
 * @return true 如果状态发生变化
 * @note   当前为占位符，完整状态机将在 Task 6 实现
 */
bool flasher_process_rx(flasher_session_t *s, const uint8_t *data, int len);

/**
 * @brief  计算当前烧录进度百分比
 * @param  s 会话状态
 * @return 进度 0-100
 */
int flasher_get_progress(const flasher_session_t *s);

#ifdef __cplusplus
}
#endif

#endif
