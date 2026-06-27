# API 调用模板与常见陷阱

> **Parent:** [doc/index.md](../index.md)

本文档汇总 UI 框架与 App 层最常用的 API 调用模板，以及新开发者最容易踩的坑。所有示例均来自实际源码，可在对应源文件中查看完整上下文。

## 创建普通列表项

```c
xerintosh_list_item_t *menu = xerintosh_new_list_item("子菜单", list_icon);
if (menu == NULL) {
    kern_log(KERN_LOG_ERROR, "failed to create menu");
}
```

*📄 Source: [app_menu_entries.c](../../src/app/app_menu_entries.c#L38-L42)*

## 创建开关项

```c
xerintosh_list_item_t *sw = xerintosh_new_switch_item(
    "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);
```

**⚠️ 陷阱**：`value` 必须指向全局或 static 变量。局部变量地址会在函数返回后悬空。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L179-L181)*

## 创建滑块项

```c
xerintosh_list_item_t *slider = xerintosh_new_slider_item(
    "亮度", &g_brightness_level, 1, 1, 10,
    NULL, on_brightness_change_cb, default_icon);
```

参数顺序：`content`、`*value`、步进、最小值、最大值、`init_callback`、`exit_callback`、图标。

**⚠️ 陷阱**：与开关项相同，`value` 必须长期有效；编辑模式下 UI 会直接读写该地址。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L209-L212)*

## 创建 user_item App 入口

```c
xerintosh_list_item_t *item = xerintosh_new_user_item(
    "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
```

三个回调签名必须一致：

```c
void app_init(void *ud);
void app_loop(void *ud);
void app_exit(void *ud);
```

**⚠️ 陷阱**：

- `init()` / `exit()` 中必须调用 `hal_input_reset_events()` 清除残留按键。
- `loop()` 中禁止调用 `xerintosh_push_pop_up()` 等显示层函数，应设标志位由主循环处理。
- `loop()` 中应调用 `ui_user_item_try_exit(event_b)` 检查退出请求。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L226-L228)*
*📄 Source: [user_item_contract.h](../../src/app/user_item_contract.h#L22-L43)*

## 挂载子项到父项

```c
if (!xerintosh_push_item_to_list(parent, child)) {
    kern_log(KERN_LOG_ERROR, "failed to push item");
}
```

**⚠️ 陷阱**：父项的子项槽位有上限 `MAX_LIST_CHILD_NUM`，超过会返回 false。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L237)*

## 清空并释放子项

```c
xerintosh_clear_children_of_list(xerintosh_get_root_list());
```

该函数会递归释放所有子节点，适合在重新构建菜单前清理。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L253)*

## 设置项转换

```c
/* 亮度等级 5 → PWM 值 127 */
int32_t pwm = settings_level_to_hw(SETTINGS_KIND_BRIGHTNESS, 5);

/* 动画速度等级 5 → 内部值 65 */
int32_t speed = settings_level_to_hw(SETTINGS_KIND_ANIM_SPEED, 5);

/* 波特率等级 5 → 115200 */
int32_t baud = settings_level_to_hw(SETTINGS_KIND_BAUD_RATE, 5);
```

*📄 Source: [settings.c](../../src/app/settings/settings.c#L254-L277)*

## 菜单拆分后的新增 App 步骤

1. 在 `src/app/<app_name>/` 实现 `xxx_init()`、`xxx_loop()`、`xxx_exit()`。
2. 在 [app_menu_entries.c](../../src/app/app_menu_entries.c#L177-L191) 的 `s_user_item_apps[]` 中新增一行。
3. 在 `s_user_item_icons[]` 中指定图标（`user_icon` 或 `default_icon`）。
4. 重新运行 `pio test -e native` 与 `pio run -e m5stick-c`。

---

> **See Also:** [UI 核心框架](../ui/index.md) | [App 层文档](../app/index.md)
