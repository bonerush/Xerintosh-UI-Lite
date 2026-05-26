/**
 * @file   sm_buffer.h
 * @brief  串口监视器终端缓冲区头文件
 * @details 定义终端环形缓冲区结构体及操作接口。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SM_BUFFER_H
#define SM_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SM_TERM_LINES    20   /* 终端缓冲区最大行数 */
#define SM_TERM_LINE_LEN 64   /* 每行最大字符数 */

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

/**
 * @brief 初始化终端缓冲区
 */
void sm_buffer_init(sm_buffer_t *buf);

/**
 * @brief  向缓冲区添加一行
 * @param  buf       缓冲区指针
 * @param  text      文本内容
 * @param  from_host 是否来自主机
 * @return true 添加成功
 * @note   当缓冲区满时，新行会覆盖最旧的行（环形覆盖）
 */
bool sm_buffer_add_line(sm_buffer_t *buf, const char *text, bool from_host);

/**
 * @brief  从缓冲区获取指定偏移的行
 * @param  buf      缓冲区指针（const）
 * @param  offset   偏移量（0 = 最新行，1 = 次新行，...）
 * @param  out      输出缓冲区
 * @param  out_len  输出缓冲区大小
 * @note   若 offset 超出有效行数范围，输出空字符串
 */
void sm_buffer_get_line(const sm_buffer_t *buf, int16_t offset,
                         char *out, size_t out_len);

/**
 * @brief  获取指定偏移行是否来自主机
 * @param  buf    缓冲区指针（const）
 * @param  offset 偏移量（0 = 最新行）
 * @return true = 主机发送，false = MCU 发出或无效偏移
 */
bool sm_buffer_get_line_source(const sm_buffer_t *buf, int16_t offset);

/**
 * @brief 清空缓冲区
 */
void sm_buffer_clear(sm_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* SM_BUFFER_H */
