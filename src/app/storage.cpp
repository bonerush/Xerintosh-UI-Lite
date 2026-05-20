#include "storage.h"

#ifdef NATIVE_TEST

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

#else

#include <Preferences.h>
#include <string.h>

static const char *NVS_NAMESPACE = "astra";

/* ─── Internal helpers ─── */

static bool read_count(Preferences *prefs, const char *key, uint8_t *out) {
    if (!prefs->isKey(key)) {
        *out = 0;
        return true;
    }
    *out = prefs->getUChar(key, 0);
    return true;
}

static bool write_count(Preferences *prefs, const char *key, uint8_t val) {
    return prefs->putUChar(key, val) == 1;
}

/* ─── Init ─── */

void storage_init(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    /* Ensure count keys exist with default 0 */
    if (!prefs.isKey("wifi_count")) {
        prefs.putUChar("wifi_count", 0);
    }
    if (!prefs.isKey("bt_count")) {
        prefs.putUChar("bt_count", 0);
    }

    prefs.end();
}

/* ─── WiFi ─── */

int storage_wifi_get_count(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t count = 0;
    read_count(&prefs,"wifi_count", count);
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
    read_count(&prefs,"wifi_count", count);
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
    read_count(&prefs,"wifi_count", count);

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

    /* Check if SSID already exists -> update password in-place */
    uint8_t count = 0;
    read_count(&prefs,"wifi_count", count);

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

    /* Append to next available slot */
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
    write_count(&prefs,"wifi_count", new_count);

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
    read_count(&prefs,"wifi_count", count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    /* Shift remaining entries down to fill the gap */
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

    /* Clear the last slot */
    char key_last_ssid[20];
    char key_last_pass[20];
    snprintf(key_last_ssid, sizeof(key_last_ssid), "wifi_ssid_%d", (int)count - 1);
    snprintf(key_last_pass, sizeof(key_last_pass), "wifi_pass_%d", (int)count - 1);
    prefs.remove(key_last_ssid);
    prefs.remove(key_last_pass);

    uint8_t new_count = count - 1;
    write_count(&prefs,"wifi_count", new_count);

    prefs.end();
    return true;
}

/* ─── Bluetooth ─── */

int storage_bt_get_count(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t count = 0;
    read_count(&prefs,"bt_count", count);
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
    read_count(&prefs,"bt_count", count);
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
    read_count(&prefs,"bt_count", count);

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

    /* Check if address already exists -> update name in-place */
    uint8_t count = 0;
    read_count(&prefs,"bt_count", count);

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

    /* Append to next available slot */
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
    write_count(&prefs,"bt_count", new_count);

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
    read_count(&prefs,"bt_count", count);
    if (index >= (int)count) {
        prefs.end();
        return false;
    }

    /* Shift remaining entries down to fill the gap */
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

    /* Clear the last slot */
    char key_last_addr[20];
    char key_last_name[20];
    snprintf(key_last_addr, sizeof(key_last_addr), "bt_addr_%d", (int)count - 1);
    snprintf(key_last_name, sizeof(key_last_name), "bt_name_%d", (int)count - 1);
    prefs.remove(key_last_addr);
    prefs.remove(key_last_name);

    uint8_t new_count = count - 1;
    write_count(&prefs,"bt_count", new_count);

    prefs.end();
    return true;
}

/* ─── Brightness ─── */

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

/* ─── Animation Speed ─── */

uint8_t storage_get_anim_speed(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("anim_speed")) {
        prefs.end();
        return 92; // default
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

/* ─── Animation Enabled ─── */

bool storage_get_anim_enabled(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("anim_enabled")) {
        prefs.end();
        return true; // default: animation on
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

/* ─── Screen Rotation ─── */

uint8_t storage_get_screen_rotation(void) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    if (!prefs.isKey("screen_rot")) {
        prefs.end();
        return 2; // default: landscape (level 2 = 90deg)
    }

    uint8_t val = prefs.getUChar("screen_rot", 1);
    prefs.end();
    return val;
}

void storage_set_screen_rotation(uint8_t val) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("screen_rot", val);
    prefs.end();
}

#endif /* NATIVE_TEST */
