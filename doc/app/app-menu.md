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
│   ├── 蓝牙（switch_item）
│   ├── 亮度（slider_item，1-10）
│   ├── 动画效果（switch_item）
│   ├── 动画速度（slider_item，1-10）
│   ├── 动画风格（switch_item）
│   ├── 弹动硬度（slider_item，1-10）
│   ├── 反弹力度（slider_item，1-10）
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

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L162-L189)*

构建完整菜单树并挂载到根节点。该函数内部调用 `flasher_menu_init()` 与 `flasher_menu_get_root()` 获取烧录器引脚子菜单。

### build_settings_items()

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L72-L117)*

构建设置子菜单的所有控件项，包括 WiFi/蓝牙开关、亮度滑条、动画效果开关、动画速度滑条，以及 Phase 2.5 新增的弹簧动画三项：

- **动画风格**（`switch_item`）：绑定 `g_spring_anim_mode`，切换弹簧/普通一阶动画
- **弹动硬度**（`slider_item`，1-10）：绑定 `g_spring_stiffness_level`
- **反弹力度**（`slider_item`，1-10）：绑定 `g_spring_damping_level`

### app_menu_push_checked()

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L122-L135)*

```c
static bool app_menu_push_checked(xerintosh_list_item_t *parent,
                                  xerintosh_list_item_t *child,
                                  const char *name)
```

安全挂载子项的 helper：

- 若 `child == NULL`，打印错误日志并返回 `false`
- 若 `xerintosh_push_item_to_list()` 返回失败，打印错误日志并返回 `false`
- 否则返回 `true`

phase 2.5 重构新增此 helper，避免菜单构建过程中因某个子项创建失败或挂载失败导致未定义行为，同时也让 `app_menu_build()` 的主流程更简洁。

#### 中文伪代码拆解

```
函数 安全挂载子项(父项, 子项, 名称) {
    if (子项 == NULL) {
        记录错误日志("app_menu: failed to create item 名称")
        return 失败
    }
    if (!挂载子项到父项(父项, 子项)) {
        记录错误日志("app_menu: failed to push item 名称")
        return 失败
    }
    return 成功
}
```

---

## 波特率选择

波特率子菜单使用 `button_item`，每个选项的 `user_data` 保存目标档位。

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L42-L65)*

```c
static xerintosh_list_item_t *build_baud_submenu(void)
{
    xerintosh_list_item_t *baud_menu = xerintosh_new_list_item("波特率", list_icon);
    if (baud_menu == NULL) {
        kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud menu");
        return NULL;
    }

    const int32_t *baud_table = settings_serial_baud_table();
    int baud_count = settings_serial_baud_count();
    for (int i = 0; i < baud_count; i++) {
        char label[16];
        snprintf(label, sizeof(label), "%ld", (long)baud_table[i]);
        xerintosh_list_item_t *btn = xerintosh_new_button_item(
            label, on_baud_selected_cb, default_icon);
        if (btn == NULL) {
            kern_log(KERN_LOG_ERROR, "app_menu: failed to create baud item %s", label);
            continue;
        }
        btn->user_data = (void *)(intptr_t)(i + 1);
        app_menu_push_checked(baud_menu, btn, label);
    }
    return baud_menu;
}
```

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L195-L201)*

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

> **See Also:** [App 初始化](app-init.md) | [烧录器](flasher.md) | [项目系统](../ui/item.md)
