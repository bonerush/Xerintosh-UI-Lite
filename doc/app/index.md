# App 层

> **Parent:** [doc/index.md](../index.md)

## 概述

App 层位于 UI 框架之上，负责构建菜单树、处理用户输入、管理设置状态与运行各子应用。本轮重构将 `app_menu.c` 拆分为框架与条目注册两部分，显式化 `user_item` 生命周期契约，并集中设置项等级到硬件值的转换。

## 核心模块

| 模块 | 源文件 | 职责 |
|---|---|---|
| 菜单框架 | [app_menu_core.h](../../src/app/app_menu_core.h)、[app_menu_core.c](../../src/app/app_menu_core.c) | `app_menu_build()` 与安全挂载辅助 |
| 菜单条目 | [app_menu_entries.h](../../src/app/app_menu_entries.h)、[app_menu_entries.c](../../src/app/app_menu_entries.c) | 设置子菜单、波特率子菜单、user_item App 注册 |
| 公共接口 | [app_menu.h](../../src/app/app_menu.h) | 对外暴露 `app_menu_build()` |
| user_item 契约 | [user_item_contract.h](../../src/app/user_item_contract.h) | `init/loop/exit` 回调类型与注册表项 |
| 设置 | [settings/settings.h](../../src/app/settings/settings.h)、[settings/settings.c](../../src/app/settings/settings.c) | 设置项加载、存取、等级↔硬件值转换 |
| 全局状态 | [app_state.h](../../src/app/app_state.h) | WiFi 开关、设置变更回调声明 |
| 输入处理 | [app_input.c](../../src/app/app_input.c) | 按键事件处理、电源键弹窗调用 |
| 电源弹窗 | [shutdown/power_key_popup.h](../../src/app/shutdown/power_key_popup.h)、[shutdown/power_key_popup.c](../../src/app/shutdown/power_key_popup.c) | 电源键弹窗状态机 |
| 初始化 | [app_init.c](../../src/app/app_init.c) | App 层初始化入口 |

## 关键概念

### user_item 生命周期

所有全屏 App 入口必须实现三个回调：

*📄 Source: [user_item_contract.h](../../src/app/user_item_contract.h#L22-L43)*

```c
typedef void (*user_item_init_fn_t)(void *ud);
typedef void (*user_item_loop_fn_t)(void *ud);
typedef void (*user_item_exit_fn_t)(void *ud);

typedef struct {
    const char *name;
    user_item_init_fn_t init;
    user_item_loop_fn_t loop;
    user_item_exit_fn_t exit;
} user_item_contract_t;
```

内置 App 统一在 `s_user_item_apps[]` 中注册：

*📄 Source: [app_menu_entries.c](../../src/app/app_menu_entries.c#L177-L191)*

```c
static const user_item_contract_t s_user_item_apps[] = {
    {"任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit},
    {"串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit},
    {"Token 消耗", token_usage_init, token_usage_loop, token_usage_exit},
    {"烧录器", flasher_init, flasher_loop, flasher_exit},
    {"示波器", oscilloscope_init, oscilloscope_loop, oscilloscope_exit},
    {"关于", about_init, about_loop, about_exit},
};
```

### 设置项统一转换

`settings_level_to_hw()` 与 `settings_hw_to_level()` 将亮度、动画速度、弹簧硬度/阻尼、波特率等映射集中到一处：

*📄 Source: [settings.c](../../src/app/settings/settings.c#L254-L314)*

```c
int32_t settings_level_to_hw(settings_kind_t kind, int16_t level);
int16_t settings_hw_to_level(settings_kind_t kind, int32_t hw);
```

### 菜单拆分

拆分前 `app_menu.c` 混合框架与条目注册；拆分后：

- `app_menu_core.c` 只负责获取根节点并调用注册函数。
- `app_menu_entries.c` 负责具体条目创建与挂载。

*📄 Source: [app_menu_core.c](../../src/app/app_menu_core.c#L46-L59)*

```c
void app_menu_build(void)
{
    xerintosh_list_item_t *root = xerintosh_get_root_list();
    /* ... */
    app_menu_register_user_item_apps(root);
    app_menu_register_settings_submenu(root);
}
```

## 中文伪代码拆解（菜单构建）

```
函数 构建应用菜单() {
    根节点 = 获取根菜单()
    if (根节点 == NULL) {
        记录错误并返回
    }

    // 第一步：注册所有全屏 App 入口
    注册_user_item_Apps(根节点)

    // 第二步：注册“设置”子菜单及其子项
    注册设置子菜单(根节点)
}
```

**核心思想**：菜单树由“框架”负责骨架，“条目注册”负责内容；新增 App 只需修改 `app_menu_entries.c` 中的注册表。

## 安全约束

- `app_menu_push_checked()` 在创建或挂载失败时打印错误并返回 false，调用方应继续处理其他项。
- `value` 指针（开关/滑块）必须指向全局变量，详见 [UI 核心框架](../ui/index.md)。
- 波特率子菜单通过 `user_data` 传递等级，回调中转换回 `int16_t`。

---

> **See Also:** [UI 核心框架](../ui/index.md) | [API 模板教程](../tutorials/api-templates.md)
