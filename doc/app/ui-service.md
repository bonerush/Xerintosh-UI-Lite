# UI 公共服务模块（UI Service）

> **Parent:** [App 层索引](index.md) | **Related:** [App 初始化](app-init.md), [项目系统](../ui/item.md)

## 概述

`ui_service` 模块为各 `user_item` App 提供统一的生命周期辅助函数，减少各 App 中重复的按键事件重置、标准退出检测等样板代码。

---

## 核心 API

*📄 Source: [ui_service.h](../../src/app/ui_service.h#L24-L50)*

```c
void ui_service_user_item_init(void);
bool ui_service_user_item_loop(hal_event_t event_b);
void ui_service_user_item_exit(void);

void ui_service_enter_landscape(void);
void ui_service_exit_landscape(void);
```

### ui_service_user_item_init()

进入 `user_item` 时调用，重置按键事件，避免进入前的残留事件被误消费。

### ui_service_user_item_loop()

每帧调用，传入按键 B 的事件，返回 `true` 表示已触发退出（`loop` 应直接 `return`）。

### ui_service_user_item_exit()

退出 `user_item` 时调用，重置按键事件，避免退出后的残留事件影响菜单导航。

### ui_service_enter_landscape()

*📄 Source: [ui_service.c](../../src/app/ui_service.c#L35-L52)*

进入全屏 App 前临时切换到横屏。该函数会：

1. 保存进入前的屏幕方向到内部静态变量 `s_prev_landscape`
2. 若当前不是横屏，则设置 `g_is_landscape = true`、`g_screen_rotation_level = ORIENTATION_LANDSCAPE`
3. 调用 `hal_display_set_rotation(1)` 并重新初始化显示

适用于需要在横屏下渲染的 `user_item`（如 `taskmgr` 三行布局、串口监视器宽屏终端）。

### ui_service_exit_landscape()

*📄 Source: [ui_service.c](../../src/app/ui_service.c#L54-L70)*

退出全屏 App 时恢复之前保存的屏幕方向。仅当进入前不是横屏时才会切回竖屏，避免不必要的显示重初始化。

---

## 使用示例

*📄 Source: [about.c](../../src/app/about/about.c#L84-L108)*

```c
void about_init(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    ui_service_user_item_init();
#endif
}

void about_loop(void *ud)
{
    (void)ud;
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (ui_service_user_item_loop(event_b)) return;

    about_draw();
}

void about_exit(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    ui_service_user_item_exit();
#endif
}
```

---

## 与 ui_user_item_try_exit 的关系

`ui_service_user_item_loop()` 内部调用 `ui_user_item_try_exit()`。提供该封装的主要目的是：

- 明确标识这是 App 层公共服务的一部分
- 便于未来扩展（例如统一保存/恢复屏幕方向、自动处理入场动画等）
- 使新 App 不需要直接引用 UI 核心层的 `ui_user_item_try_exit()`

---

> **See Also:** [App 初始化](app-init.md) | [项目系统](../ui/item.md) | [从零开始创建 App](../tutorials/your-first-app.md)
