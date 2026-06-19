# 存储模块（Storage）

> **Parent:** [App 层索引](index.md) | **Related:** [设置管理](settings.md), [WiFi 管理器](wifi.md), [烧录器](flasher.md)

## 概述

`storage` 模块是 NVS（Non-Volatile Storage）持久化的薄封装。硬件环境使用 ESP32 Arduino `Preferences` 库，Native 测试环境提供固定默认值的桩实现。所有设置项、WiFi 凭据、DeepSeek API Key、烧录器引脚角色等均通过本模块读写。

## 关键概念

### 双实现架构

*📄 Source: [storage.cpp](../../src/app/storage/storage.cpp#L14-L58)*

```c
#ifdef NATIVE_TEST

void     storage_init(void) {}
int      storage_wifi_get_count(void) { return 0; }
bool     storage_wifi_get(int index, char *ssid, char *pass) { ... }
int16_t  storage_get_brightness(void) { return -1; }
...

#else

/* 硬件环境：ESP32 Preferences 实现 */
#include <Preferences.h>
...

#endif /* NATIVE_TEST */
```

- **Native 测试**：所有函数返回固定默认值，不依赖硬件
- **硬件环境**：每个 `storage_get_*` / `storage_set_*` 函数独立打开/关闭 `Preferences` 会话，避免长期持有 NVS 句柄

### 通用凭据存储

*📄 Source: [storage.cpp](../../src/app/storage/storage.cpp#L121-L135)*

```c
typedef struct {
    const char *count_key;
    const char *field1_fmt;
    const char *field2_fmt;
    uint8_t     max_items;
    size_t      field1_max;
    size_t      field2_max;
} credential_kind_t;

static const credential_kind_t WIFI_KIND = {
    "wifi_count", "wifi_ssid_%d", "wifi_pass_%d",
    STORAGE_MAX_WIFI_NETWORKS, STORAGE_SSID_MAX_LEN, STORAGE_PASS_MAX_LEN
};
```

WiFi 凭据通过统一的 `credential_kind_t` 描述符读写，消除 count/field 格式重复代码。当前仅 WiFi 使用，结构保留给未来 BT 凭据扩展。

### settings 使用的存储接口

*📄 Source: [storage.h](../../src/app/storage/storage.h#L82-L182)*

```c
int16_t  storage_get_brightness(void);
void     storage_set_brightness(int16_t val);

uint8_t  storage_get_anim_speed(void);
void     storage_set_anim_speed(uint8_t val);

bool     storage_get_anim_enabled(void);
void     storage_set_anim_enabled(bool val);

bool     storage_get_spring_mode(void);
void     storage_set_spring_mode(bool val);

int16_t  storage_get_spring_stiffness(void);
void     storage_set_spring_stiffness(int16_t val);

int16_t  storage_get_spring_damping(void);
void     storage_set_spring_damping(int16_t val);

uint8_t  storage_get_screen_rotation(void);
void     storage_set_screen_rotation(uint8_t val);

int16_t  storage_get_serial_baud_rate(void);
void     storage_set_serial_baud_rate(int16_t val);
```

`settings_load_from_storage()` 和 `storage_save_all()` 通过上述接口与 NVS 交互，settings 模块本身不直接访问 `Preferences`。

### 批量保存 / 加载

*📄 Source: [storage.cpp](../../src/app/storage/storage.cpp#L496-L510)*

```c
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
```

`storage_save_all()` 集中保存当前所有设置档位；`storage_load_all()` 是 `settings_load_from_storage()` 的薄封装，供 Shell 等非 settings 模块调用。

---

> **See Also:** [设置管理](settings.md) | [WiFi 管理器](wifi.md) | [烧录器](flasher.md)
