# App 菜单构建模块（App Menu）

> **Parent:** [App 层索引](index.md) | **Related:** [App 初始化](app-init.md), [项目系统](../ui/item.md)

## 概述

`app_menu` 模块负责构建 Xerintosh UI 菜单树。phase 2.4 重构后，该职责从 `app_init.c` 中独立出来，使菜单结构变更不再需要修改入口封装文件。

---

## 菜单结构

```
根菜单
├── 设置
│   ├── WiFi（switch_item）
│   ├── 亮度（slider_item，1-10）
│   ├── 动画效果（switch_item）
│   ├── 动画速度（slider_item，1-10）
│   ├── 横屏/竖屏（switch_item）
│   ├── 烧录器引脚（子菜单，由 flasher_menu.c 提供）
│   └── 波特率（子菜单）
├── 任务管理器（user_item）
├── 串口监视器（user_item）
├── Token Usage（user_item）
├── 烧录器（user_item）
└── 关于（user_item）
```

---

## 核心 API

### app_menu_build()

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L32-L103)*

构建完整菜单树并挂载到根节点。该函数内部调用 `flasher_menu_init()` 与 `flasher_menu_get_root()` 获取烧录器引脚子菜单。

---

## 波特率选择

波特率子菜单使用 `button_item`，每个选项的 `user_data` 保存目标档位。

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L108-L116)*

```c
static void on_baud_selected_cb(void *ud)
{
    int16_t level = (int16_t)(intptr_t)ud;
    g_serial_baud_rate = level;
    on_serial_baud_change_cb(NULL);
    xerintosh_selector_exit_current_item();
}
```

重构后，回调直接使用 `ud` 参数，不再依赖全局选择器指针。

---

## 与 flasher_menu 的边界

`app_menu.c` 不直接处理烧录器引脚配置 UI，而是通过 `flasher_menu.c` 暴露的接口挂载：

```c
flasher_menu_init();
xerintosh_list_item_t* flasher_pin_menu = flasher_menu_get_root();
xerintosh_push_item_to_list(item1, flasher_pin_menu);
```

这样，烧录器引脚的角色分配、强制解除状态机等复杂逻辑完全封装在 `src/app/flasher/` 内部。

---

> **See Also:** [App 初始化](app-init.md) | [烧录器菜单](../app/flasher-menu.md) | [项目系统](../ui/item.md)
