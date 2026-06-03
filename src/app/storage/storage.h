/**
 * @file   storage.h
 * @brief  NVS 存储管理头文件
 * @details 提供 WiFi/蓝牙凭据、亮度、动画速度、动画开关、屏幕方向的持久化存储接口。
 *          所有 API 均为 C 兼容接口，底层在硬件环境使用 ESP32 Preferences 库。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define STORAGE_MAX_WIFI_NETWORKS  5   /* 最大保存 WiFi 网络数 */
#define STORAGE_MAX_BT_DEVICES     5   /* 最大保存蓝牙设备数 */
#define STORAGE_SSID_MAX_LEN       33  /* SSID 最大长度（含 null） */
#define STORAGE_PASS_MAX_LEN       65  /* 密码最大长度（含 null） */
#define STORAGE_BT_NAME_MAX_LEN    33  /* 蓝牙名称最大长度 */
#define STORAGE_BT_ADDR_MAX_LEN    18  /* 蓝牙 MAC 地址长度（含 null） */

/* ═══ 生命周期 ═══ */

/**
 * @brief 初始化存储命名空间，确保计数 key 存在
 */
void storage_init(void);

/* ═══ WiFi 凭据 ═══ */

/**
 * @brief  获取已保存的 WiFi 网络数量
 * @return 网络数量
 */
int      storage_wifi_get_count(void);

/**
 * @brief  获取指定索引的 WiFi 凭据
 * @param  index 索引（0-based）
 * @param  ssid  输出缓冲区（至少 STORAGE_SSID_MAX_LEN 字节）
 * @param  pass  输出缓冲区（至少 STORAGE_PASS_MAX_LEN 字节）
 * @return true  获取成功
 * @return false 索引越界或不存在
 */
bool     storage_wifi_get(int index, char *ssid, char *pass);

/**
 * @brief  查找指定 SSID 的索引
 * @param  ssid SSID 字符串
 * @return 索引（>=0）；未找到返回 -1
 */
int      storage_wifi_find(const char *ssid);

/**
 * @brief  添加或更新 WiFi 凭据
 * @param  ssid SSID
 * @param  pass 密码
 * @return true  添加/更新成功
 * @return false 存储已满或参数无效
 */
bool     storage_wifi_add(const char *ssid, const char *pass);

/**
 * @brief  删除指定索引的 WiFi 凭据
 * @param  index 索引
 * @return true  删除成功
 * @return false 索引越界
 */
bool     storage_wifi_remove(int index);

/* ═══ BT 凭据 ═══ */

/**
 * @brief  查找指定 MAC 地址的索引
 * @param  addr MAC 地址字符串
 * @return 索引（>=0）；未找到返回 -1
 */
int      storage_bt_find(const char *addr);

/**
 * @brief  添加或更新蓝牙设备信息
 * @param  addr MAC 地址
 * @param  name 设备名称
 * @return true  添加/更新成功
 * @return false 存储已满或参数无效
 */
bool     storage_bt_add(const char *addr, const char *name);

/**
 * @brief  删除指定索引的蓝牙设备
 * @param  index 索引
 * @return true  删除成功
 * @return false 索引越界
 */
bool     storage_bt_remove(int index);

/* ═══ 亮度 ═══ */

/**
 * @brief  从存储读取亮度值
 * @return 亮度值；未设置时返回 -1
 */
int16_t  storage_get_brightness(void);

/**
 * @brief  保存亮度值到存储
 * @param  val 亮度值
 */
void     storage_set_brightness(int16_t val);

/* ═══ 动画速度 ═══ */

/**
 * @brief  从存储读取动画速度值
 * @return 动画速度值；未设置时返回默认值 92
 */
uint8_t  storage_get_anim_speed(void);

/**
 * @brief  保存动画速度值到存储
 * @param  val 动画速度值
 */
void     storage_set_anim_speed(uint8_t val);

/* ═══ 动画开关 ═══ */

/**
 * @brief  从存储读取动画开关状态
 * @return true  开；未设置时默认返回 true
 */
bool     storage_get_anim_enabled(void);

/**
 * @brief  保存动画开关状态到存储
 * @param  val 开关状态
 */
void     storage_set_anim_enabled(bool val);

/* ═══ 屏幕旋转 ═══ */

/**
 * @brief  从存储读取屏幕方向值
 * @return 方向值（1=竖屏, 2=横屏）；未设置时默认返回 2
 */
uint8_t  storage_get_screen_rotation(void);

/**
 * @brief  保存屏幕方向值到存储
 * @param  val 方向值
 */
void     storage_set_screen_rotation(uint8_t val);

/* ═══ 串口波特率 ═══ */

/**
 * @brief  从存储读取串口波特率等级
 * @return 波特率等级（1-6）；未设置时默认返回 5（115200）
 */
int16_t  storage_get_serial_baud_rate(void);

/**
 * @brief  保存串口波特率等级到存储
 * @param  val 波特率等级（1-6）
 */
void     storage_set_serial_baud_rate(int16_t val);

/* ═══ API Key ═══ */

#define STORAGE_API_KEY_MAX_LEN  64

/**
 * @brief  从存储读取 Deepseek API Key
 * @param  key   输出缓冲区
 * @param  max_len 缓冲区大小
 * @return true  读取成功
 * @return false 未设置
 */
bool     storage_get_deepseek_key(char *key, size_t max_len);

/**
 * @brief  保存 Deepseek API Key 到存储
 * @param  key API Key 字符串
 */
void     storage_set_deepseek_key(const char *key);

/**
 * @brief  从存储读取 Kimi API Key
 * @param  key   输出缓冲区
 * @param  max_len 缓冲区大小
 * @return true  读取成功
 * @return false 未设置
 */
bool     storage_get_kimi_key(char *key, size_t max_len);

/**
 * @brief  保存 Kimi API Key 到存储
 * @param  key API Key 字符串
 */
void     storage_set_kimi_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
