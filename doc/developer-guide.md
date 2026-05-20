# Xerintosh UI Lite 开发者指南

> 本文档介绍如何基于 Xerintosh UI Lite 框架设计菜单结构、创建自定义 App，以及推荐的项目组织方式。
>
> 关于各模块内部实现，请参考同目录下的 `core.md`、`item.md`、`drawer.md`、`draw-driver.md` 及 `hal/` 下的文档。

---

## 目录

1. [框架概述](#1-框架概述)
2. [菜单树结构设计](#2-菜单树结构设计)
3. [五种菜单项类型](#3-五种菜单项类型)
4. [自定义 App（user_item）](#4-自定义-appuser_item)
5. [输入交互映射](#5-输入交互映射)
6. [代码组织建议](#6-代码组织建议)
7. [完整示例](#7-完整示例)
8. [注意事项与限制](#8-注意事项与限制)

---

## 1. 框架概述

Xerintosh UI Lite 是一个面向嵌入式设备的层级菜单框架，采用**树形数据模型** + **动画渲染引擎** + **硬件抽象层（HAL）** 的三层架构。

```
App 代码
    │
    ▼
app_init.h/c         ← 菜单树构建、管理器初始化、输入处理
settings.h/c         ← 亮度/动画/方向配置与存储
    │
    ▼
ui_item.h/c          ← 数据模型：菜单树、选择器、相机
    │
    ▼
ui_core.h/c          ← 动画引擎与主循环调度
    │
    ▼
ui_drawer.h/c        ← 渲染管线：列表、选择器、弹窗、信息栏
    │
    ▼
ui_draw_driver.h/c   ← 宏桥接：oled_* → hal_*
    │
    ▼
hal_display.h/cpp    ← 显示抽象（M5Canvas / 内存帧缓冲）
hal_input.h/cpp      ← 按键输入抽象
hal_system.h/cpp     ← 系统时钟与延时
```

开发者只需关注 **UI Item 层** 和 **App 层**：定义菜单树、实现自定义 `user_item` 的回调即可。动画、渲染、输入处理均由框架自动完成。

---

## 2. 菜单树结构设计

### 2.1 核心概念

菜单在内存中以**多叉树**形式组织：

- **根节点（root）**：由框架自动创建的隐式节点，不显示在界面上
- **子节点**：挂载在父节点下的菜单项，最多 10 个
- **层级（layer）**：根节点为 0，每向下一层 +1，最大 10 层

### 2.2 构建菜单树

所有菜单项通过 `xerintosh_push_item_to_list(parent, child)` 挂载到树上。该函数会自动：

1. 设置子节点的 `layer = parent->layer + 1`
2. 计算子节点的纵向目标坐标 `y_list_item_trg`
3. 如果是根节点的第一个子项，自动绑定到选择器和相机

```c
#include "ui/ui_item.h"

xerintosh_list_item_t* root = xerintosh_get_root_list();  // 获取（或自动创建）根节点

xerintosh_list_item_t* settings = xerintosh_new_list_item("设置", list_icon);
xerintosh_list_item_t* about    = xerintosh_new_list_item("关于", user_icon);

xerintosh_push_item_to_list(root, settings);
xerintosh_push_item_to_list(root, about);
```

### 2.3 层级示例

```
root（隐式，不显示）
├── 设置
│   ├── WiFi          [switch_item]
│   └── 亮度          [slider_item]
├── 工具
│   └── 时钟          [user_item ← 自定义App]
└── 关于              [list_item]
```

对应代码：

```c
xerintosh_list_item_t* root     = xerintosh_get_root_list();
xerintosh_list_item_t* settings = xerintosh_new_list_item("设置", list_icon);
xerintosh_list_item_t* tools    = xerintosh_new_list_item("工具", list_icon);
xerintosh_list_item_t* about    = xerintosh_new_list_item("关于", flag_icon);

static bool wifi_on = false;
static int16_t brightness = 50;

xerintosh_push_item_to_list(settings, xerintosh_new_switch_item("WiFi", &wifi_on, NULL, NULL, switch_icon));
xerintosh_push_item_to_list(settings, xerintosh_new_slider_item("亮度", &brightness, 5, 0, 100, NULL, NULL, slider_icon));
xerintosh_push_item_to_list(tools, xerintosh_new_user_item("时钟", clock_init, clock_loop, clock_exit, user_icon));

xerintosh_push_item_to_list(root, settings);
xerintosh_push_item_to_list(root, tools);
xerintosh_push_item_to_list(root, about);
```

---

## 3. 五种菜单项类型

| 类型 | 结构体 | 用途 | 确认键行为 |
|------|--------|------|-----------|
| `list_item` | `xerintosh_list_item_t` | 普通菜单/子菜单 | 进入子菜单 |
| `switch_item` | `xerintosh_switch_item_t` | 布尔开关 | 翻转布尔值 |
| `slider_item` | `xerintosh_slider_item_t` | 数值调节 | 进入/确认数值编辑模式 |
| `button_item` | `xerintosh_button_item_t` | 单次触发按钮 | 执行回调函数 |
| `user_item` | `xerintosh_user_item_t` | **自定义 App/全屏界面** | 进入自定义界面 |

### 3.1 list_item

用于组织子菜单，本身不携带额外数据。

```c
xerintosh_list_item_t* item = xerintosh_new_list_item("菜单名", list_icon);
```

### 3.2 switch_item

绑定一个 `bool*` 指针，界面上会显示一个开关图形。

```c
static bool wifi_on = false;

xerintosh_list_item_t* sw = xerintosh_new_switch_item(
    "WiFi",           // 显示文本
    &wifi_on,         // 绑定的布尔变量（必须持久有效）
    NULL,             // init_function：进入该项时调用（可选）
    NULL,             // exit_function：值改变后调用（可选）
    switch_icon       // 图标
);
```

**注意**：绑定的变量必须是 `static` 或全局变量，不能是局部变量，因为框架只保存指针。

### 3.3 slider_item

绑定一个 `int16_t*` 指针，支持步进、最小值、最大值。

```c
static int16_t brightness = 50;

xerintosh_list_item_t* sl = xerintosh_new_slider_item(
    "亮度",
    &brightness,      // 绑定的数值变量
    5,                // 步进值（每按一次增减 5）
    0,                // 最小值
    100,              // 最大值
    NULL,             // init_function（可选）
    NULL,             // exit_function（可选）
    slider_icon
);
```

**交互流程**：
1. 首次长按 **B（确认）**：进入编辑模式，备份原值，选择器宽度变窄
2. 短按 **A/B**：在编辑模式下增减数值
3. 再次长按 **B（确认）**：确认修改，退出编辑模式
4. 长按 **A（返回）**：取消修改，恢复原值，退出编辑模式

### 3.4 button_item

没有状态存储，仅用于触发一次动作。

```c
void on_reboot()
{
    xerintosh_push_pop_up("正在重启...", 2000);
    delay(1000);
    ESP.restart();
}

xerintosh_list_item_t* btn = xerintosh_new_button_item("重启", on_reboot, power_icon);
```

### 3.5 user_item（自定义 App）

这是开发自定义界面的核心类型。它有三个生命周期回调：

```c
xerintosh_list_item_t* app = xerintosh_new_user_item(
    "时钟",           // 菜单显示名称
    clock_init,       // init_function：进入时调用一次
    clock_loop,       // loop_function：进入后每帧调用
    clock_exit,       // exit_function：退出时调用一次
    user_icon         // 图标
);
```

**生命周期时序**：

```
用户长按 B（确认）
    │
    ▼
播放进入动画（exit_animation）
    │
    ▼
调用 init_function()      ← 只调用一次
    │
    ▼
设置 in_user_item = true
    │
    ▼
每帧调用 loop_function()   ← 约 60fps
    │
    ▼
用户长按 A（返回）
    │
    ▼
播放退出动画
    │
    ▼
调用 exit_function()       ← 只调用一次
    │
    ▼
设置 in_user_item = false，返回菜单列表
```

---

## 4. 自定义 App（user_item）

### 4.1 基本实现模板

```cpp
#include <M5Unified.h>
#include "ui/ui_item.h"

static uint32_t start_time = 0;

void my_app_init()
{
    // 一次性初始化：分配资源、重置状态
    start_time = millis();
}

void my_app_loop()
{
    // 每帧执行：完全控制屏幕绘制
    // 框架不会绘制菜单列表、选择器等任何 UI 元素

    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);

    uint32_t elapsed = (millis() - start_time) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", elapsed / 60, elapsed % 60);

    int16_t tw = M5.Display.textWidth(buf);
    int16_t th = 16;
    M5.Display.setCursor((SCREEN_WIDTH - tw) / 2, (SCREEN_HEIGHT - th) / 2);
    M5.Display.print(buf);

    // 提示返回方式
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, SCREEN_HEIGHT - 10);
    M5.Display.print("长按A返回");
}

void my_app_exit()
{
    // 清理资源：释放内存、关闭外设、保存状态等
    start_time = 0;
}
```

### 4.2 在 user_item 中使用框架通知

即使在 `user_item` 内部，也可以调用框架的通知 API：

```c
// 顶部信息栏（自动收回）
xerintosh_push_info_bar("连接成功", 1500);

// 中部弹窗（自动收回）
xerintosh_push_pop_up("已保存", 1000);
```

### 4.3 在 user_item 中读取输入（高级）

如果你需要在 `loop_function` 中自定义按键行为（例如游戏），可以直接读取 HAL 输入状态：

```cpp
#include "hal/hal_input.h"

void my_game_loop()
{
    hal_input_update();  // 刷新按键状态

    if (hal_input_is_pressed(HAL_BTN_A)) {
        // A 键正被按住
    }

    hal_event_t ev = hal_input_get_event(HAL_BTN_B);
    if (ev == HAL_EVENT_SHORT_PRESS) {
        // B 键短按事件（每帧调用会消费事件，注意时序）
    }

    // ... 绘制游戏画面
}
```

**注意**：
- `app_init.c` 的 `app_input_process()` 仍然在每帧运行，长按 A 仍会触发退出逻辑
- 如果你需要完全接管按键（例如 A 键在游戏中也有用），建议在 `init_function` 中设置一个全局标志，在 `app_init.c` 的 `app_input_process()` 中判断该标志以跳过框架导航

---

## 5. 输入交互映射

框架默认的按键映射（定义在 `app_init.c` 的 `app_input_process()`）：

| 按键 | 短按 | 长按 |
|------|------|------|
| **BtnA** | 选择器上移一项 | 返回上一层 / 退出当前项 |
| **BtnB** | 选择器下移一项 | 确认 / 进入选中项 |

### 5.1 特殊状态下的按键行为

| 当前状态 | 短按 A | 短按 B | 长按 A | 长按 B |
|----------|--------|--------|--------|--------|
| 普通列表 | 上移 | 下移 | 返回上级 | 进入/确认 |
| `slider_item` 编辑模式 | 数值 -step | 数值 +step | 取消，恢复原值 | 确认修改 |
| `user_item` 内部 | 由 App 决定 | 由 App 决定 | 退出 App | 由 App 决定 |

### 5.2 修改按键映射

如果你希望自定义按键行为（例如交换 A/B 功能），修改 `app_init.c` 中的 `app_input_process()`：

```c
void app_input_process(void)
{
    hal_input_update();

    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    /* 示例：交换 A/B 的短按功能 */
    if (event_a == HAL_EVENT_SHORT_PRESS)
        xerintosh_selector_go_next_item();   /* A 短按改为下移 */
    else if (event_a == HAL_EVENT_LONG_PRESS)
        xerintosh_selector_exit_current_item();

    if (event_b == HAL_EVENT_SHORT_PRESS)
        xerintosh_selector_go_prev_item();   /* B 短按改为上移 */
    else if (event_b == HAL_EVENT_LONG_PRESS)
        xerintosh_selector_jump_to_selected_item();
}
```

---

## 6. 代码组织建议

本项目采用分层架构，各层职责已在 [知识地图](../index.md) 中说明。新增功能时，遵循以下原则：

### 6.1 不要修改框架层

- `ui/` 目录下的文件是 UI 框架核心，不要直接修改
- `hal/` 目录下的文件是硬件抽象层，如需支持新硬件，新增 HAL 实现文件而非修改现有文件

### 6.2 在 App 层扩展功能

当前项目已提供标准的 App 层模块：

```
src/
├── main.cpp              # Arduino 入口，保持精简
├── app/
│   ├── app_init.c        # 菜单树构建、管理器初始化
│   ├── app_init.h
│   ├── settings.c        # 亮度/动画/方向配置
│   ├── settings.h
│   ├── storage.cpp       # NVS 存储封装
│   ├── storage.h
│   ├── wifi_manager.cpp  # WiFi 管理器
│   ├── wifi_manager.h
│   ├── bt_manager.cpp    # 蓝牙管理器
│   ├── bt_manager.h
│   └── serial_input.cpp  # 串口输入处理
├── hal/                  # 硬件抽象层（不修改）
└── ui/                   # UI 框架核心（不修改）
```

**新增自定义 App（user_item）时**，建议创建独立的 `app_xxx.c` 文件：

```c
/* app_clock.c */
#include "ui/ui_item.h"
#include "hal/hal_system.h"
#include "hal/hal_display.h"

static uint32_t g_start_time = 0;

void app_clock_init(void)
{
    g_start_time = hal_get_ticks();
}

void app_clock_loop(void)
{
    uint32_t elapsed = (hal_get_ticks() - g_start_time) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", elapsed / 60, elapsed % 60);

    hal_display_clear();
    hal_draw_utf8(10, 30, buf, COLOR_FG);
}

void app_clock_exit(void)
{
    g_start_time = 0;
}
```

然后在 `app_init.c` 中挂载：

```c
#include "app_clock.h"

void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();
    xerintosh_list_item_t* tools = xerintosh_new_list_item("工具", list_icon);

    xerintosh_push_item_to_list(tools, xerintosh_new_user_item(
        "时钟", app_clock_init, app_clock_loop, app_clock_exit, user_icon));

    xerintosh_push_item_to_list(root, tools);
}
```

### 6.3 main.cpp 保持精简

重构后的 `main.cpp` 只负责：

1. 调用 `M5.begin()` 初始化硬件
2. 调用 `settings_load_from_storage()` 恢复设置
3. 调用 `app_init_ui()` 构建菜单
4. 调用 `app_init_managers()` 初始化 WiFi/BT
5. 每帧调用 `app_input_process()` 处理输入
6. 调用渲染管线完成一帧绘制

**不要在 `main.cpp` 中直接编写菜单构建逻辑或业务逻辑**，这些应提取到 `app_init.c` 或独立的 App 模块中。

---

## 7. 完整示例

以下是一个可直接编译运行的完整菜单结构示例，展示了所有五种 Item 类型的用法：

```cpp
#include "ui/ui_item.h"

// ========== 状态变量 ==========
static bool wifi_on     = false;
static bool ble_on      = false;
static int16_t volume   = 30;
static int16_t contrast = 50;

// ========== 按钮回调 ==========
void on_factory_reset()
{
    xerintosh_push_pop_up("已恢复出厂设置", 2000);
}

void on_about_exit()
{
    // About 页面退出时的清理
}

// ========== 自定义 App 回调 ==========
void sensor_app_init()
{
    // 初始化传感器
}

void sensor_app_loop()
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 30);
    M5.Display.print("Sensor Data");
    // ... 读取并显示传感器数据
}

void sensor_app_exit()
{
    // 关闭传感器
}

// ========== 构建菜单 ==========
void build_main_menu()
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    // -- 设置 --
    xerintosh_list_item_t* settings = xerintosh_new_list_item("设置", list_icon);
    xerintosh_push_item_to_list(settings, xerintosh_new_switch_item("WiFi", &wifi_on, NULL, NULL, switch_icon));
    xerintosh_push_item_to_list(settings, xerintosh_new_switch_item("蓝牙", &ble_on, NULL, NULL, switch_icon));
    xerintosh_push_item_to_list(settings, xerintosh_new_slider_item("音量", &volume, 5, 0, 100, NULL, NULL, slider_icon));
    xerintosh_push_item_to_list(settings, xerintosh_new_slider_item("对比度", &contrast, 10, 0, 255, NULL, NULL, slider_icon));

    // -- 工具 --
    xerintosh_list_item_t* tools = xerintosh_new_list_item("工具", list_icon);
    xerintosh_push_item_to_list(tools, xerintosh_new_user_item("传感器", sensor_app_init, sensor_app_loop, sensor_app_exit, user_icon));
    xerintosh_push_item_to_list(tools, xerintosh_new_button_item("恢复出厂", on_factory_reset, power_icon));

    // -- 关于 --
    xerintosh_list_item_t* about = xerintosh_new_list_item("关于", flag_icon);

    // -- 挂载到根 --
    xerintosh_push_item_to_list(root, settings);
    xerintosh_push_item_to_list(root, tools);
    xerintosh_push_item_to_list(root, about);
}
```

---

## 8. 注意事项与限制

### 8.1 数据生命周期

`switch_item`、`slider_item` 绑定的变量地址会被框架长期持有。务必确保这些变量在菜单整个生命周期内有效：

```cpp
// ✅ 正确：静态/全局变量
static bool wifi_on = false;
xerintosh_new_switch_item("WiFi", &wifi_on, ...);

// ❌ 错误：局部变量，函数返回后指针悬空
void bad_example() {
    bool wifi = false;
    xerintosh_new_switch_item("WiFi", &wifi, ...);  // 危险！
}
```

### 8.2 内存限制

框架使用 `malloc` 分配菜单节点内存。每个节点约 100~200 字节。ESP32 内存有限，避免创建过多菜单项。

### 8.3 层级与数量限制

| 限制 | 值 | 说明 |
|------|-----|------|
| `MAX_LIST_CHILD_NUM` | 10 | 每个父节点最多 10 个子项 |
| `MAX_LIST_LAYER` | 10 | 菜单树最大深度 10 层 |

超过限制时 `xerintosh_push_item_to_list()` 返回 `false`。

### 8.4 user_item 中的导航

在 `user_item` 的 `loop_function` 中：
- 不要调用 `xerintosh_selector_go_next_item()`、`xerintosh_selector_go_prev_item()` 等导航函数，这会破坏菜单状态
- 如果需要自定义按键行为，参考第 4.3 节，通过全局标志让 `input_process()` 跳过框架导航

### 8.5 屏幕方向与分辨率

当前框架针对 **M5Stick-C（80x160 TFT）** 设计，在 `main.cpp` 中设置了 `setRotation(1)`（横屏）。如果你更换设备或方向，需检查以下常量：

- `SCREEN_WIDTH`、`SCREEN_HEIGHT`（定义在 `ui_draw_driver.h` 或 `hal_display.h`）
- 列表项间距 `LIST_ITEM_SPACING`
- 选择器高度、字体大小等

### 8.6 图标选择

```c
default_icon    // 自动根据类型选择默认图标
list_icon       // 列表/子菜单
switch_icon     // 开关
plus_icon       // 加号/按钮
user_icon       // 用户/应用
slider_icon     // 滑块
flag_icon       // 标记/关于
power_icon      // 电源/系统
```

---

## 附录：常用 API 速查

### 菜单构建

| 函数 | 说明 |
|------|------|
| `xerintosh_get_root_list()` | 获取根节点（单例） |
| `xerintosh_new_list_item(content, icon)` | 创建普通菜单项 |
| `xerintosh_new_switch_item(content, value, init, exit, icon)` | 创建开关 |
| `xerintosh_new_slider_item(content, value, step, min, max, init, exit, icon)` | 创建滑块 |
| `xerintosh_new_button_item(content, exit, icon)` | 创建按钮 |
| `xerintosh_new_user_item(content, init, loop, exit, icon)` | 创建自定义 App |
| `xerintosh_push_item_to_list(parent, child)` | 挂载子项到父项 |

### 导航控制

| 函数 | 说明 |
|------|------|
| `xerintosh_selector_go_next_item()` | 选择器下移 |
| `xerintosh_selector_go_prev_item()` | 选择器上移 |
| `xerintosh_selector_jump_to_selected_item()` | 确认/进入当前项 |
| `xerintosh_selector_exit_current_item()` | 返回/退出当前项 |

### 通知

| 函数 | 说明 |
|------|------|
| `xerintosh_push_info_bar(content, span_ms)` | 顶部信息栏 |
| `xerintosh_push_pop_up(content, span_ms)` | 中部弹窗 |

### 状态查询

| 函数 | 说明 |
|------|------|
| `xerintosh_is_in_user_item()` | 当前是否在某个 user_item 内部 |
| `in_xerintosh` | 全局布尔：UI 是否激活 |
