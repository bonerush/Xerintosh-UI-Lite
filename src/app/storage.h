#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 常量 ─── */

#define STORAGE_MAX_WIFI_NETWORKS  5
#define STORAGE_MAX_BT_DEVICES     5
#define STORAGE_SSID_MAX_LEN       33
#define STORAGE_PASS_MAX_LEN       65
#define STORAGE_BT_NAME_MAX_LEN    33
#define STORAGE_BT_ADDR_MAX_LEN    18

/* ─── 生命周期 ─── */

void storage_init(void);

/* ─── WiFi 凭据 ─── */

int      storage_wifi_get_count(void);
bool     storage_wifi_get(int index, char *ssid, char *pass);
int      storage_wifi_find(const char *ssid);
bool     storage_wifi_add(const char *ssid, const char *pass);
bool     storage_wifi_remove(int index);

/* ─── BT 凭据 ─── */

int      storage_bt_get_count(void);
bool     storage_bt_get(int index, char *addr, char *name);
int      storage_bt_find(const char *addr);
bool     storage_bt_add(const char *addr, const char *name);
bool     storage_bt_remove(int index);

/* ─── 亮度 ─── */

int16_t  storage_get_brightness(void);
void     storage_set_brightness(int16_t val);

/* ─── 动画速度 ─── */

uint8_t  storage_get_anim_speed(void);
void     storage_set_anim_speed(uint8_t val);

/* ─── 动画开关 ─── */

bool     storage_get_anim_enabled(void);
void     storage_set_anim_enabled(bool val);

/* ─── 屏幕旋转 ─── */

uint8_t  storage_get_screen_rotation(void);
void     storage_set_screen_rotation(uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
