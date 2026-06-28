/**
 * @file   storage.h
 * @brief  NVS 存储管理头文件
 * @details 提供 WiFi 凭据、亮度、动画速度、动画开关、屏幕方向的持久化存储接口。
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
#define STORAGE_SSID_MAX_LEN       33  /* SSID 最大长度（含 null） */
#define STORAGE_PASS_MAX_LEN       65  /* 密码最大长度（含 null） */

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

/* ═══ 弹簧动画设置（Round 10+） ═══ */

/**
 * @brief  从存储读取弹簧动画模式（true=动弹弹簧, false=普通一阶）
 * @return 模式值；未设置时默认返回 true
 */
bool     storage_get_spring_mode(void);

/**
 * @brief  保存弹簧动画模式到存储
 * @param  val 模式值
 */
void     storage_set_spring_mode(bool val);

/**
 * @brief  从存储读取弹簧刚度等级（1-10）
 * @return 等级值；未设置时默认返回 5
 */
int16_t  storage_get_spring_stiffness(void);

/**
 * @brief  保存弹簧刚度等级到存储
 * @param  val 等级值（1-10）
 */
void     storage_set_spring_stiffness(int16_t val);

/**
 * @brief  从存储读取弹簧阻尼等级（1-10）
 * @return 等级值；未设置时默认返回 9
 */
int16_t  storage_get_spring_damping(void);

/**
 * @brief  保存弹簧阻尼等级到存储
 * @param  val 等级值（1-10）
 */
void     storage_set_spring_damping(int16_t val);

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

/* ═══ 主题模式 ═══ */

/**
 * @brief  从存储读取主题模式
 * @return true=黑夜模式；未设置时默认返回 true
 */
bool storage_get_theme(void);

/**
 * @brief  保存主题模式到存储
 * @param  val true=黑夜模式, false=白天模式
 */
void storage_set_theme(bool val);

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

/* ═══ 烧录器引脚映射 ═══ */

/**
 * @brief  从存储读取烧录器指定引脚的角色
 * @param  pin 引脚编号
 * @return 信号角色枚举值；未设置时默认返回 0（NONE）
 */
uint8_t storage_get_flasher_pin_role(uint8_t pin);

/**
 * @brief  保存烧录器指定引脚的角色到存储
 * @param  pin  引脚编号
 * @param  role 信号角色枚举值
 */
void    storage_set_flasher_pin_role(uint8_t pin, uint8_t role);

/* ═══ 批量保存 / 加载 ═══ */

/**
 * @brief 将当前所有设置项保存到 NVS 存储
 * @note  调用各 storage_set_* 接口，保存亮度、动画、方向、波特率、弹簧参数等。
 */
void storage_save_all(void);

/**
 * @brief 从 NVS 存储重新加载所有设置项到全局变量
 * @note  当前由 settings_load_from_storage() 实现；本函数为其薄封装，
 *        供 Shell 等非 settings 模块调用。
 */
void storage_load_all(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
