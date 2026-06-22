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
#include "app/settings/settings.h"

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
int16_t  storage_get_brightness(void) { return -1; }  /* 模拟 NVS 中无保存值 */
void     storage_set_brightness(int16_t val) { (void)val; }
uint8_t  storage_get_anim_speed(void) { return 92; }
void     storage_set_anim_speed(uint8_t val) { (void)val; }
bool     storage_get_anim_enabled(void) { return true; }
void     storage_set_anim_enabled(bool val) { (void)val; }
bool     storage_get_spring_mode(void) { return true; }
void     storage_set_spring_mode(bool val) { (void)val; }
int16_t  storage_get_spring_stiffness(void) { return 5; }
void     storage_set_spring_stiffness(int16_t val) { (void)val; }
int16_t  storage_get_spring_damping(void) { return 9; }
void     storage_set_spring_damping(int16_t val) { (void)val; }
uint8_t  storage_get_screen_rotation(void) { return 2; }
void     storage_set_screen_rotation(uint8_t val) { (void)val; }
int16_t  storage_get_serial_baud_rate(void) { return 5; }
void     storage_set_serial_baud_rate(int16_t val) { (void)val; }
bool     storage_get_deepseek_key(char *key, size_t max_len) {
    (void)max_len; key[0] = '\0'; return false;
}
void     storage_set_deepseek_key(const char *key) { (void)key; }

uint8_t storage_get_flasher_pin_role(uint8_t pin) {
    (void)pin;
    return 0; /* default NONE */
}
void storage_set_flasher_pin_role(uint8_t pin, uint8_t role) {
    (void)pin; (void)role;
}

void storage_save_all(void) {}
void storage_load_all(void) {}

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

/* ─── 通用凭据存储辅助 ─── */

/**
 * @brief 凭据类型描述符（消除 wifi/bt 之间的代码重复）
 */
typedef struct {
    const char *count_key;       /* NVS 计数 key（如 "wifi_count"） */
    const char *field1_fmt;      /* 字段1 key 格式（如 "wifi_ssid_%%d"） */
    const char *field2_fmt;      /* 字段2 key 格式（如 "wifi_pass_%%d"） */
    uint8_t     max_items;       /* 最大条目数 */
    size_t      field1_max;      /* 字段1 缓冲区最大长度 */
    size_t      field2_max;      /* 字段2 缓冲区最大长度 */
} credential_kind_t;

static const credential_kind_t WIFI_KIND = {
    "wifi_count", "wifi_ssid_%d", "wifi_pass_%d",
    STORAGE_MAX_WIFI_NETWORKS, STORAGE_SSID_MAX_LEN, STORAGE_PASS_MAX_LEN
};

static int cred_get_count(const credential_kind_t *k) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t count = 0;
    read_count(&prefs, k->count_key, &count);
    prefs.end();
    return (int)count;
}

static bool cred_get(const credential_kind_t *k, int index,
                     char *f1, char *f2) {
    if (index < 0 || index >= k->max_items) return false;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, k->count_key, &count);
    if (index >= (int)count) { prefs.end(); return false; }

    char key1[20], key2[20];
    snprintf(key1, sizeof(key1), k->field1_fmt, index);
    snprintf(key2, sizeof(key2), k->field2_fmt, index);

    size_t len1 = prefs.getString(key1, f1, k->field1_max);
    prefs.getString(key2, f2, k->field2_max);
    prefs.end();
    return (len1 > 0);
}

static int cred_find(const credential_kind_t *k, const char *target) {
    if (!target) return -1;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    uint8_t count = 0;
    read_count(&prefs, k->count_key, &count);

    char buf[STORAGE_PASS_MAX_LEN];  /* 足够容纳 wifi_pass 或 bt_name */
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), k->field1_fmt, i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, target) == 0) { prefs.end(); return i; }
    }
    prefs.end();
    return -1;
}

static bool cred_add(const credential_kind_t *k, const char *f1, const char *f2) {
    if (!f1 || !f2) return false;
    if (strlen(f1) == 0) return false;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    uint8_t count = 0;
    read_count(&prefs, k->count_key, &count);

    /* 已存在则原地更新字段2 */
    char buf[STORAGE_PASS_MAX_LEN];
    for (int i = 0; i < (int)count; i++) {
        char key[20];
        snprintf(key, sizeof(key), k->field1_fmt, i);
        prefs.getString(key, buf, sizeof(buf));
        if (strcmp(buf, f1) == 0) {
            char key2[20];
            snprintf(key2, sizeof(key2), k->field2_fmt, i);
            prefs.putString(key2, f2);
            prefs.end();
            return true;
        }
    }

    if (count >= k->max_items) { prefs.end(); return false; }

    char key1[20], key2[20];
    snprintf(key1, sizeof(key1), k->field1_fmt, (int)count);
    snprintf(key2, sizeof(key2), k->field2_fmt, (int)count);
    prefs.putString(key1, f1);
    prefs.putString(key2, f2);
    write_count(&prefs, k->count_key, count + 1);
    prefs.end();
    return true;
}

static bool cred_remove(const credential_kind_t *k, int index) {
    if (index < 0 || index >= k->max_items) return false;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    uint8_t count = 0;
    read_count(&prefs, k->count_key, &count);
    if (index >= (int)count) { prefs.end(); return false; }

    /* 前移后续条目 */
    char buf[STORAGE_PASS_MAX_LEN];
    for (int i = index; i < (int)count - 1; i++) {
        char key_src1[20], key_dst1[20], key_src2[20], key_dst2[20];
        snprintf(key_src1, sizeof(key_src1), k->field1_fmt, i + 1);
        snprintf(key_dst1, sizeof(key_dst1), k->field1_fmt, i);
        snprintf(key_src2, sizeof(key_src2), k->field2_fmt, i + 1);
        snprintf(key_dst2, sizeof(key_dst2), k->field2_fmt, i);

        prefs.getString(key_src1, buf, sizeof(buf));
        prefs.putString(key_dst1, buf);
        prefs.getString(key_src2, buf, sizeof(buf));
        prefs.putString(key_dst2, buf);
    }

    /* 清空最后一个槽位 */
    char key_last1[20], key_last2[20];
    snprintf(key_last1, sizeof(key_last1), k->field1_fmt, (int)count - 1);
    snprintf(key_last2, sizeof(key_last2), k->field2_fmt, (int)count - 1);
    prefs.remove(key_last1);
    prefs.remove(key_last2);
    write_count(&prefs, k->count_key, count - 1);
    prefs.end();
    return true;
}

/* ═══ WiFi 凭据 ═══ */

int storage_wifi_get_count(void) { return cred_get_count(&WIFI_KIND); }

bool storage_wifi_get(int index, char *ssid, char *pass) {
    return cred_get(&WIFI_KIND, index, ssid, pass);
}

int storage_wifi_find(const char *ssid) {
    return cred_find(&WIFI_KIND, ssid);
}

bool storage_wifi_add(const char *ssid, const char *pass) {
    return cred_add(&WIFI_KIND, ssid, pass);
}

bool storage_wifi_remove(int index) {
    return cred_remove(&WIFI_KIND, index);
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

/* ═══ 弹簧动画设置（Round 10+） ═══ */

bool storage_get_spring_mode(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("spring_mode")) {
        prefs.end();
        return true; /* 默认动弹 */
    }

    bool val = prefs.getBool("spring_mode", true);
    prefs.end();
    return val;
}

void storage_set_spring_mode(bool val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool("spring_mode", val);
    prefs.end();
}

int16_t storage_get_spring_stiffness(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("spring_stiff")) {
        prefs.end();
        return 5; /* 默认刚度等级 5 → 0.20 */
    }

    int16_t val = (int16_t)prefs.getUChar("spring_stiff", 5);
    prefs.end();
    return val;
}

void storage_set_spring_stiffness(int16_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("spring_stiff", (uint8_t)val);
    prefs.end();
}

int16_t storage_get_spring_damping(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("spring_damp")) {
        prefs.end();
        return 9; /* 默认阻尼等级 9 → 0.36 */
    }

    int16_t val = (int16_t)prefs.getUChar("spring_damp", 9);
    prefs.end();
    return val;
}

void storage_set_spring_damping(int16_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("spring_damp", (uint8_t)val);
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

/* ═══ API Key ═══ */

bool storage_get_deepseek_key(char *key, size_t max_len) {
    if (!key || max_len == 0) return false;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t len = prefs.getString("ds_key_v1", key, max_len);
    prefs.end();
    return len > 0;
}

void storage_set_deepseek_key(const char *key) {
    if (!key) return;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("ds_key_v1", key);
    prefs.end();
}

uint8_t storage_get_flasher_pin_role(uint8_t pin) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    char key[16];
    snprintf(key, sizeof(key), "flash_pin%u", pin);
    uint8_t val = prefs.getUChar(key, 0);
    prefs.end();
    return val;
}

void storage_set_flasher_pin_role(uint8_t pin, uint8_t role) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    char key[16];
    snprintf(key, sizeof(key), "flash_pin%u", pin);
    prefs.putUChar(key, role);
    prefs.end();
}

void storage_save_all(void)
{
    storage_set_brightness(settings_get_brightness());
    storage_set_anim_speed((uint8_t)settings_get_anim_speed());
    storage_set_anim_enabled(g_anim_enabled);
    storage_set_screen_rotation((uint8_t)settings_get_rotation());
    storage_set_spring_stiffness(settings_get_spring_stiffness());
    storage_set_spring_damping(settings_get_spring_damping());
    storage_set_serial_baud_rate(settings_get_baud_rate());
}

void storage_load_all(void)
{
    settings_load_from_storage();
}

#endif /* NATIVE_TEST */
