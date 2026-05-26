/**
 * @file   storage.cpp
 * @brief  NVS 存储管理实现
 * @details 双实现架构：
 *          - NATIVE_TEST 时：返回固定默认值（桩实现）
 *          - 硬件环境时：使用 ESP32 Preferences 库进行 NVS 持久化存储
 *
 * @copyright Copyright (c) 2026
 */

#include "storage.h"

#ifdef NATIVE_TEST

/* ═══ Native 测试环境：存储桩 ═══ */

void     storage_init(void) {}
int      storage_wifi_get_count(void) { return 0; }
bool     storage_wifi_get(int index, char *ssid, char *pass) {
    (void)index; (void)ssid; (void)pass; return false;
}
int      storage_wifi_find(const char *ssid) { (void)ssid; return -1; }
bool     storage_wifi_add(const char *ssid, const char *pass) {
    (void)ssid; (void)pass; return false;
}
bool     storage_wifi_remove(int index) { (void)index; return false; }
int      storage_bt_get_count(void) { return 0; }
bool     storage_bt_get(int index, char *addr, char *name) {
    (void)index; (void)addr; (void)name; return false;
}
int      storage_bt_find(const char *addr) { (void)addr; return -1; }
bool     storage_bt_add(const char *addr, const char *name) {
    (void)addr; (void)name; return false;
}
bool     storage_bt_remove(int index) { (void)index; return false; }
int16_t  storage_get_brightness(void) { return 50; }
void     storage_set_brightness(int16_t val) { (void)val; }
uint8_t  storage_get_anim_speed(void) { return 92; }
void     storage_set_anim_speed(uint8_t val) { (void)val; }
bool     storage_get_anim_enabled(void) { return true; }
void     storage_set_anim_enabled(bool val) { (void)val; }
uint8_t  storage_get_screen_rotation(void) { return 2; }
void     storage_set_screen_rotation(uint8_t val) { (void)val; }
int16_t  storage_get_serial_baud_rate(void) { return 5; }
void     storage_set_serial_baud_rate(int16_t val) { (void)val; }

#else

/* ═══ 硬件环境：ESP32 Preferences 实现 ═══ */

#include <Preferences.h>
#include <string.h>

static const char *NVS_NAMESPACE = "Xerintosh";  /* NVS 命名空间 */

/* ─── 内部辅助函数 ─── */

/**
 * @brief 从 Preferences 读取计数 key
 */
static bool read_count(Preferences *prefs, const char *key, uint8_t *out) {
    if (!prefs->isKey(key)) {
        *out = 0;
        return true;
    }
    *out = prefs->getUChar(key, 0);
    return true;
}

/**
 * @brief 向 Preferences 写入计数 key
 */
static bool write_count(Preferences *prefs, const char *key, uint8_t val) {
    return prefs->putUChar(key, val) == 1;
}

/* ═══ 初始化 ═══ */

/**
 * @brief 初始化存储命名空间，确保计数 key 存在
 * @note  屏幕方向使用新 key "screen_orient"，使旧版竖屏默认值在首次启动时被忽略
 */
void storage_init(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    /* 确保计数 key 存在，默认值为 0 */
    if (!prefs.isKey("wifi_count")) {
        prefs.putUChar("wifi_count", 0);
    }
    if (!prefs.isKey("bt_count")) {
        prefs.putUChar("bt_count", 0);
    }

    /* 默认屏幕方向：横屏（level 2） */
    if (!prefs.isKey("screen_orient")) {
        prefs.putUChar("screen_orient", 2); /* 横屏 */
    }

    prefs.end();
}

/* ═══ WiFi 凭据 ═══ */

int storage_wifi_get_count(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t count = 0;
    read_count(&prefs, "wifi_count", &count);
    prefs.end();
    return (int)count;
}

bool storage_wifi_get(int index, char *ssid, char *pass) {
    if (index < 0 || index >= STORAGE_MAX_WIFI_NETWORKS) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, "wifi_count", &count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    char key_ssid[20];
    char key_pass[20];
    snprintf(key_ssid, sizeof(key_ssid), "wifi_ssid_%d", index);
    snprintf(key_pass, sizeof(key_pass), "wifi_pass_%d", index);

    size_t ssid_len = prefs.getString(key_ssid, ssid, STORAGE_SSID_MAX_LEN);
    size_t pass_len = prefs.getString(key_pass, pass, STORAGE_PASS_MAX_LEN);

    prefs.end();
    return (ssid_len > 0);
}

int storage_wifi_find(const char *ssid) {
    if (!ssid) {
        return -1;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, "wifi_count", &count);

    char buf[STORAGE_SSID_MAX_LEN];
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), "wifi_ssid_%d", i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, ssid) == 0) {
            prefs.end();
            return i;
        }
    }

    prefs.end();
    return -1;
}

bool storage_wifi_add(const char *ssid, const char *pass) {
    if (!ssid || !pass) {
        return false;
    }
    if (strlen(ssid) == 0) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    /* 若 SSID 已存在则原地更新密码 */
    uint8_t count = 0;
    read_count(&prefs, "wifi_count", &count);

    char buf[STORAGE_SSID_MAX_LEN];
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), "wifi_ssid_%d", i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, ssid) == 0) {
            char key_pass[20];
            snprintf(key_pass, sizeof(key_pass), "wifi_pass_%d", i);
            prefs.putString(key_pass, pass);
            prefs.end();
            return true;
        }
    }

    /* 追加到下一个空槽位 */
    if (count >= STORAGE_MAX_WIFI_NETWORKS) {
        prefs.end();
        return false;
    }

    char key_ssid[20];
    char key_pass[20];
    snprintf(key_ssid, sizeof(key_ssid), "wifi_ssid_%d", (int)count);
    snprintf(key_pass, sizeof(key_pass), "wifi_pass_%d", (int)count);

    prefs.putString(key_ssid, ssid);
    prefs.putString(key_pass, pass);

    uint8_t new_count = count + 1;
    write_count(&prefs, "wifi_count", new_count);

    prefs.end();
    return true;
}

bool storage_wifi_remove(int index) {
    if (index < 0 || index >= STORAGE_MAX_WIFI_NETWORKS) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    uint8_t count = 0;
    read_count(&prefs, "wifi_count", &count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    /* 将后续条目前移填补空缺 */
    for (int i = index; i < (int)count - 1; i++) {
        char key_src_ssid[20];
        char key_dst_ssid[20];
        char key_src_pass[20];
        char key_dst_pass[20];

        snprintf(key_src_ssid, sizeof(key_src_ssid), "wifi_ssid_%d", i + 1);
        snprintf(key_dst_ssid, sizeof(key_dst_ssid), "wifi_ssid_%d", i);
        snprintf(key_src_pass, sizeof(key_src_pass), "wifi_pass_%d", i + 1);
        snprintf(key_dst_pass, sizeof(key_dst_pass), "wifi_pass_%d", i);

        char buf[STORAGE_PASS_MAX_LEN];

        prefs.getString(key_src_ssid, buf, sizeof(buf));
        prefs.putString(key_dst_ssid, buf);

        prefs.getString(key_src_pass, buf, sizeof(buf));
        prefs.putString(key_dst_pass, buf);
    }

    /* 清空最后一个槽位 */
    char key_last_ssid[20];
    char key_last_pass[20];
    snprintf(key_last_ssid, sizeof(key_last_ssid), "wifi_ssid_%d", (int)count - 1);
    snprintf(key_last_pass, sizeof(key_last_pass), "wifi_pass_%d", (int)count - 1);
    prefs.remove(key_last_ssid);
    prefs.remove(key_last_pass);

    uint8_t new_count = count - 1;
    write_count(&prefs, "wifi_count", new_count);

    prefs.end();
    return true;
}

/* ═══ 蓝牙凭据 ═══ */

int storage_bt_get_count(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t count = 0;
    read_count(&prefs, "bt_count", &count);
    prefs.end();
    return (int)count;
}

bool storage_bt_get(int index, char *addr, char *name) {
    if (index < 0 || index >= STORAGE_MAX_BT_DEVICES) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, "bt_count", &count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    char key_addr[20];
    char key_name[20];
    snprintf(key_addr, sizeof(key_addr), "bt_addr_%d", index);
    snprintf(key_name, sizeof(key_name), "bt_name_%d", index);

    size_t addr_len = prefs.getString(key_addr, addr, STORAGE_BT_ADDR_MAX_LEN);
    size_t name_len = prefs.getString(key_name, name, STORAGE_BT_NAME_MAX_LEN);

    prefs.end();
    return (addr_len > 0);
}

int storage_bt_find(const char *addr) {
    if (!addr) {
        return -1;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, "bt_count", &count);

    char buf[STORAGE_BT_ADDR_MAX_LEN];
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), "bt_addr_%d", i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, addr) == 0) {
            prefs.end();
            return i;
        }
    }

    prefs.end();
    return -1;
}

bool storage_bt_add(const char *addr, const char *name) {
    if (!addr || !name) {
        return false;
    }
    if (strlen(addr) == 0) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    /* 若地址已存在则原地更新名称 */
    uint8_t count = 0;
    read_count(&prefs, "bt_count", &count);

    char buf[STORAGE_BT_ADDR_MAX_LEN];
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), "bt_addr_%d", i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, addr) == 0) {
            char key_name[20];
            snprintf(key_name, sizeof(key_name), "bt_name_%d", i);
            prefs.putString(key_name, name);
            prefs.end();
            return true;
        }
    }

    /* 追加到下一个空槽位 */
    if (count >= STORAGE_MAX_BT_DEVICES) {
        prefs.end();
        return false;
    }

    char key_addr[20];
    char key_name[20];
    snprintf(key_addr, sizeof(key_addr), "bt_addr_%d", (int)count);
    snprintf(key_name, sizeof(key_name), "bt_name_%d", (int)count);

    prefs.putString(key_addr, addr);
    prefs.putString(key_name, name);

    uint8_t new_count = count + 1;
    write_count(&prefs, "bt_count", new_count);

    prefs.end();
    return true;
}

bool storage_bt_remove(int index) {
    if (index < 0 || index >= STORAGE_MAX_BT_DEVICES) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    uint8_t count = 0;
    read_count(&prefs, "bt_count", &count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    /* 将后续条目前移填补空缺 */
    for (int i = index; i < (int)count - 1; i++) {
        char key_src_addr[20];
        char key_dst_addr[20];
        char key_src_name[20];
        char key_dst_name[20];

        snprintf(key_src_addr, sizeof(key_src_addr), "bt_addr_%d", i + 1);
        snprintf(key_dst_addr, sizeof(key_dst_addr), "bt_addr_%d", i);
        snprintf(key_src_name, sizeof(key_src_name), "bt_name_%d", i + 1);
        snprintf(key_dst_name, sizeof(key_dst_name), "bt_name_%d", i);

        char buf[STORAGE_BT_NAME_MAX_LEN];

        prefs.getString(key_src_addr, buf, sizeof(buf));
        prefs.putString(key_dst_addr, buf);

        prefs.getString(key_src_name, buf, sizeof(buf));
        prefs.putString(key_dst_name, buf);
    }

    /* 清空最后一个槽位 */
    char key_last_addr[20];
    char key_last_name[20];
    snprintf(key_last_addr, sizeof(key_last_addr), "bt_addr_%d", (int)count - 1);
    snprintf(key_last_name, sizeof(key_last_name), "bt_name_%d", (int)count - 1);
    prefs.remove(key_last_addr);
    prefs.remove(key_last_name);

    uint8_t new_count = count - 1;
    write_count(&prefs, "bt_count", new_count);

    prefs.end();
    return true;
}

/* ═══ 亮度 ═══ */

int16_t storage_get_brightness(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("brightness")) {
        prefs.end();
        return -1;
    }

    int16_t val = prefs.getShort("brightness", 50);
    prefs.end();
    return val;
}

void storage_set_brightness(int16_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putShort("brightness", val);
    prefs.end();
}

/* ═══ 动画速度 ═══ */

uint8_t storage_get_anim_speed(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("anim_speed")) {
        prefs.end();
        return 92; /* 默认动画速度 */
    }

    uint8_t val = prefs.getUChar("anim_speed", 92);
    prefs.end();
    return val;
}

void storage_set_anim_speed(uint8_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("anim_speed", val);
    prefs.end();
}

/* ═══ 动画开关 ═══ */

bool storage_get_anim_enabled(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("anim_enabled")) {
        prefs.end();
        return true; /* 默认开启动画 */
    }

    bool val = prefs.getBool("anim_enabled", true);
    prefs.end();
    return val;
}

void storage_set_anim_enabled(bool val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool("anim_enabled", val);
    prefs.end();
}

/* ═══ 屏幕旋转 ═══ */

uint8_t storage_get_screen_rotation(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("screen_orient")) {
        prefs.end();
        return 2; /* 默认横屏（level 2 = 90deg） */
    }

    uint8_t val = prefs.getUChar("screen_orient", 2);
    prefs.end();
    return val;
}

void storage_set_screen_rotation(uint8_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("screen_orient", val);
    prefs.end();
}

/* ═══ 串口波特率 ═══ */

int16_t storage_get_serial_baud_rate(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("serial_baud_v1")) {
        prefs.end();
        return 5; /* 默认 115200 */
    }

    int16_t val = prefs.getShort("serial_baud_v1", 5);
    prefs.end();
    return val;
}

void storage_set_serial_baud_rate(int16_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putShort("serial_baud_v1", val);
    prefs.end();
}

#endif /* NATIVE_TEST */
