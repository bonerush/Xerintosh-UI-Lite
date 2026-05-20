# C 语言 OOP 编码风格规范

> **Parent:** [知识地图](index.md)
>
> 本规范定义本项目所有 C 代码（含 C++ 实现但对暴露 C 接口的模块）必须遵循的编码风格。目标是通过统一的命名、模块封装和结构体继承模式，使代码易读、易维护、易扩展。

---

## 1. 命名规范

### 1.1 模块前缀

每个模块必须有唯一的**两到三个字母前缀**，所有导出符号都必须以此前缀开头。

| 模块 | 前缀 | 示例 |
|------|------|------|
| UI 核心 | `astra_` | `astra_init_core()` |
| 硬件抽象-显示 | `hal_display_` | `hal_display_clear()` |
| 硬件抽象-输入 | `hal_input_` | `hal_input_update()` |
| 设置管理 | `settings_` | `settings_load_from_storage()` |
| WiFi 管理 | `wifi_mgr_` | `wifi_mgr_enable()` |
| 蓝牙管理 | `bt_mgr_` | `bt_mgr_init()` |
| 存储 | `storage_` | `storage_get_brightness()` |

### 1.2 命名规则

```c
/* 函数：模块前缀 + snake_case */
void astra_init_core(void);
int16_t settings_brightness_hw_value(void);

/* 结构体：模块前缀 + _t */
typedef struct { ... } astra_list_item_t;
typedef struct { ... } hal_display_t;

/* 枚举：模块前缀 + _type_t */
typedef enum { ... } astra_list_item_type_t;

/* 回调类型：模块前缀 + _cb_t */
typedef void (*astra_item_cb_t)(void *user_data);

/* 全局变量（模块内部）：g_ 前缀 */
static int16_t g_brightness_level = 5;

/* 宏/常量：UPPER_SNAKE_CASE */
#define MAX_LIST_CHILD_NUM 10
#define SCREEN_WIDTH 80

/* 参数：前导下划线（本项目沿用 convention） */
void astra_animation(float *_pos, float _pos_trg, float _speed);
```

---

## 2. 模块封装模式

### 2.1 单例结构体 + 访问函数

每个模块封装自己的状态到一个结构体中，对外只暴露访问函数。

```c
/* settings.h */
typedef struct {
    int16_t brightness_level;
    int16_t anim_speed_level;
    bool    anim_enabled;
    int16_t screen_rotation_level;
} settings_t;

settings_t *settings_get(void);
void settings_load_from_storage(void);

/* settings.c */
static settings_t g_settings = {5, 5, true, 2};

settings_t *settings_get(void) {
    return &g_settings;
}

void settings_load_from_storage(void) {
    /* 从 NVS 恢复 ... */
}
```

### 2.2 头文件模板

所有 `.h` 文件必须遵循以下模板：

```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 类型定义 ─── */

/* ─── 模块单例访问 ─── */

/* ─── 生命周期 ─── */

/* ─── 操作函数 ─── */

/* ─── 回调类型 ─── */

#ifdef __cplusplus
}
#endif

#endif /* MODULE_NAME_H */
```

---

## 3. 结构体继承模式

### 3.1 基类结构体

基类结构体包含所有派生类共享的字段，**第一个字段必须是类型标签**。

```c
typedef enum {
    list_item,
    switch_item,
    slider_item,
    user_item,
    button_item,
} astra_list_item_type_t;

typedef struct astra_list_item_t {
    astra_list_item_type_t type;   /* 类型标签：必须放第一位 */
    astra_list_item_icon_t icon;
    const char *content;
    /* ... 其他公共字段 */
} astra_list_item_t;
```

### 3.2 派生结构体

派生结构体**必须把基类作为第一个成员**，这是 C 标准保证的内存布局兼容。

```c
typedef struct astra_switch_item_t {
    astra_list_item_t base_item;   /* 必须放第一位！ */
    bool *value;
    void (*init_cb)(void *user_data);
    void (*exit_cb)(void *user_data);
    void *user_data;
} astra_switch_item_t;
```

### 3.3 安全类型转换

永远不要直接强制转换，必须通过类型标签检查。

```c
static astra_list_item_t *astra_safe_cast(astra_list_item_t *_item,
                                          astra_list_item_type_t _expected)
{
    if (_item != NULL && _item->type == _expected)
        return _item;
    return astra_get_root_list();  /* 降级到 root，避免空指针崩溃 */
}

astra_switch_item_t *astra_to_switch_item(astra_list_item_t *_item) {
    return (astra_switch_item_t *)astra_safe_cast(_item, switch_item);
}
```

**为什么基类必须放第一位**：C 标准保证结构体的第一个成员偏移量为 0。因此 `(astra_list_item_t*)switch_item_ptr` 是安全的，指针值不变，只是解释方式不同。

---

## 4. 回调设计

### 4.1 统一签名

所有回调统一使用 `void (*)(void *user_data)` 签名，并附带 `user_data` 上下文指针。

```c
typedef void (*astra_callback_t)(void *user_data);

/* 在结构体中 */
struct astra_user_item_t {
    astra_list_item_t base_item;
    astra_callback_t init_cb;
    astra_callback_t loop_cb;
    astra_callback_t exit_cb;
    void *user_data;  /* 回调上下文 */
};
```

### 4.2 使用方式

```c
/* 注册回调时传递上下文 */
astra_list_item_t *item = astra_new_user_item(
    "我的页面",
    my_init,      /* astra_callback_t */
    my_loop,
    my_exit,
    &my_context   /* void *user_data */
);

/* 调用时传递上下文 */
if (_item->init_cb != NULL) {
    _item->init_cb(_item->user_data);
}
```

---

## 5. C/C++ 边界规范

### 5.1 原则

- **所有对外暴露的头文件必须是 C 兼容的**
- **与 C++ 库交互的模块保留 `.cpp` 后缀**，但对外只暴露 C 接口
- **内部实现尽量使用 C 语法**，避免 `nullptr`、`&` 引用、类等 C++ 特有语法

### 5.2 示例：HAL 显示模块

```cpp
/* hal_display.cpp — 内部使用 M5GFX (C++) */
#include <M5Unified.h>
#include <M5GFX.h>

static M5Canvas* g_canvas = nullptr;  /* C++ 内部使用 nullptr 没问题 */

/* 对外暴露纯 C 接口 */
extern "C" void hal_display_init(void) {
    g_canvas = new M5Canvas(&M5.Display);
    /* ... */
}

extern "C" void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    g_canvas->drawPixel(x, y, color);
}
```

---

## 6. 文件组织

### 6.1 配对原则

每个模块必须有对应的 `.h` 和 `.c`（或 `.cpp`）文件，文件名前缀一致。

```
src/app/
├── settings.h      /* 接口 */
├── settings.c      /* C 实现 */
├── app_init.h      /* 接口 */
└── app_init.c      /* C 实现 */
```

### 6.2 目录镜像

`doc/` 目录结构与 `src/` 目录结构保持镜像关系。

```
doc/
├── index.md
├── ui/             /* 对应 src/ui/ */
├── hal/            /* 对应 src/hal/ */
└── app/            /* 对应 src/app/ */
    ├── settings.md
    └── app-init.md
```

---

## 7. 禁止清单

| 禁止项 | 正确做法 | 说明 |
|--------|----------|------|
| `nullptr` | `NULL` | C 标准使用 NULL |
| `Preferences &prefs` | `Preferences *prefs` | C 使用指针传递 |
| `uint8_t &out` | `uint8_t *out` | C 使用指针传递 |
| `static const size_t X = 65;` | `#define X 65` | C 使用宏定义常量 |
| 直接 `extern` 访问其他模块全局变量 | 使用 getter/setter | 封装模块状态 |
| 函数指针签名不统一 | 统一 `void (*)(void*)` | 便于上下文传递 |

---

## 8. 快速检查清单

新增或修改模块时，确认：

- [ ] 所有导出函数有模块前缀
- [ ] 头文件有 `extern "C"` 保护
- [ ] 头文件有 include guard
- [ ] 结构体继承时基类放第一位
- [ ] 类型转换有安全检查
- [ ] 回调统一带 `user_data`
- [ ] 没有 `nullptr`、`&` 引用等 C++ 语法出现在 C 接口中
- [ ] 文档已同步更新
