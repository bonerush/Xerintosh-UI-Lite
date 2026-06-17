# Token Usage App

> **Parent:** [App 层索引](index.md) | **Related:** [UI 公共服务](ui-service.md), [设置模块](settings.md)

## 概述

`token_usage` App 用于显示 DeepSeek API 的 Token 用量统计。它通过 `tu_api.cpp` 发起 HTTP 请求，由 `tu_ui.cpp` 渲染结果，并遵循标准的 `user_item` 生命周期（`init` / `loop` / `exit`）。

---

## 模块结构

| 文件 | 职责 |
|------|------|
| `src/app/token_usage/tu_app.cpp/h` | App 生命周期、刷新调度、空 key 保护 |
| `src/app/token_usage/tu_api.cpp/h` | DeepSeek API 客户端 |
| `src/app/token_usage/tu_ui.cpp/h` | 用量数据渲染 |
| `src/app/token_usage/token_usage.h` | 公开生命周期接口（供 `app_menu.c` 使用） |

---

## 核心 API

### 测试可见 getter

*📄 Source: [tu_app.h](../../src/app/token_usage/tu_app.h#L24-L26)*

```c
const tu_data_t* token_usage_get_data(void);
```

返回当前 Token 用量数据的只读指针，主要用于 native 测试断言。返回的是文件作用域静态变量 `g_tu_data` 的地址，**不要**在 App 外部修改其内容。

*📄 Source: [tu_app.cpp](../../src/app/token_usage/tu_app.cpp#L26-L29)*

```c
const tu_data_t* token_usage_get_data(void)
{
    return &g_tu_data;
}
```

### 空 API Key 跳过网络请求

*📄 Source: [tu_app.cpp](../../src/app/token_usage/tu_app.cpp#L62-L70)*

```c
/* 获取 API key */
char ds_key[STORAGE_API_KEY_MAX_LEN];
bool has_key = storage_get_deepseek_key(ds_key, sizeof(ds_key));

/* 刷新数据（空 key 时跳过请求） */
if (has_key && ds_key[0] != '\0') {
    g_tu_data.deepseek_ok = tu_api_fetch_deepseek(ds_key, &g_tu_data.deepseek);
} else {
    g_tu_data.deepseek_ok = false;
}
```

刷新逻辑在 phase 2.5 增加空 key 保护：

- 若 `storage_get_deepseek_key()` 返回 false，或 key 为空字符串，则直接置 `g_tu_data.deepseek_ok = false`
- 只有存在非空 key 时才调用 `tu_api_fetch_deepseek()`，避免向服务器发送无意义的请求

### 自动刷新机制

*📄 Source: [tu_app.h](../../src/app/token_usage/tu_app.h#L22)*

```c
#define TU_REFRESH_INTERVAL 30000  /* 自动刷新间隔（毫秒） */
```

- 进入 App 时 `g_needs_refresh = true`，首次 `loop` 立即刷新
- 之后每 30 秒自动刷新一次
- 短按 BtnA 可手动触发刷新（设置 `g_needs_refresh = true`）

### 生命周期

*📄 Source: [tu_app.cpp](../../src/app/token_usage/tu_app.cpp#L31-L88)*

```c
void token_usage_init(void *ud)
{
    (void)ud;
    tu_data_init(&g_tu_data);
    g_last_refresh  = 0;
    g_needs_refresh = true;
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}

void token_usage_loop(void *ud)
{
    (void)ud;

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (event_a == HAL_EVENT_SHORT_PRESS) {
        g_needs_refresh = true;
    }

    if (ui_user_item_try_exit(event_b)) return;

    uint32_t now = hal_get_ticks();
    if (g_needs_refresh || (now - g_last_refresh >= TU_REFRESH_INTERVAL)) {
        char ds_key[STORAGE_API_KEY_MAX_LEN];
        bool has_key = storage_get_deepseek_key(ds_key, sizeof(ds_key));

        if (has_key && ds_key[0] != '\0') {
            g_tu_data.deepseek_ok = tu_api_fetch_deepseek(ds_key, &g_tu_data.deepseek);
        } else {
            g_tu_data.deepseek_ok = false;
        }
        g_tu_data.last_update = now;

        g_last_refresh  = now;
        g_needs_refresh = false;
    }

    tu_ui_draw(&g_tu_data, 0);
}

void token_usage_exit(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    hal_input_reset_events();
#endif
}
```

---

## 数据模型

`tu_data_t` 由 `tu_api.h` 定义，主要字段包括：

| 字段 | 含义 |
|------|------|
| `deepseek_ok` | 最近一次请求是否成功 |
| `deepseek` | DeepSeek 用量明细结构体 |
| `last_update` | 上次刷新时间戳（毫秒） |

---

## 与其他组件的关系

- **storage**：通过 `storage_get_deepseek_key()` 读取持久化的 API key
- **ui_service**：未来可接入 `ui_service_enter_landscape()` / `ui_service_exit_landscape()` 切换横屏展示更多列
- **tu_api**：负责网络请求与 JSON 解析
- **tu_ui**：负责把 `tu_data_t` 渲染到屏幕

---

> **See Also:** [App 层索引](index.md) | [UI 公共服务](ui-service.md) | [从零开始创建 App](../tutorials/your-first-app.md)
