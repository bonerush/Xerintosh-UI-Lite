# 从零开始：创建你的第一个 Xerintosh UI App

> **Parent:** [知识地图](../index.md) | **Related:** [开发者指南](../developer-guide.md), [项目系统](../ui/item.md), [应用初始化](../app/app-init.md)
>
> 本教程面向初次接触本框架的开发者。不需要预先了解 OLED 或 TFT 驱动，只需要具备基础 C 语言知识即可。

---

## 本章目标

读完本章后，你将能够：

1. 理解框架的三层架构和菜单树概念
2. 创建一个新的 App（`user_item`）并注册到菜单
3. 使用 HAL API 在屏幕上绘制文字和图形
4. 处理按键输入，实现简单的交互逻辑

---

## Step 1：理解框架的"三层蛋糕"

在动手写代码之前，先花 3 分钟理解框架的整体结构。这能帮你避免"我在哪一层写代码"的困惑。

### 三层架构

框架像一块分层的蛋糕：

```
┌─────────────────────────────────────────┐
│  第 3 层：App 层（你写代码的地方）        │
│  my_app.c / app_menu.c / settings.c     │
│  职责：定义菜单树、实现业务逻辑           │
├─────────────────────────────────────────┤
│  第 2 层：UI Core 层（框架核心）          │
│  ui_item.c / ui_core.c / ui_drawer.c    │
│  职责：动画引擎、渲染管线、选择器导航      │
├─────────────────────────────────────────┤
│  第 1 层：HAL 层（硬件抽象）              │
│  hal_display.cpp / hal_input.cpp        │
│  职责：把"画一个圆"翻译成 M5GFX 指令     │
└─────────────────────────────────────────┘
```

**关键原则**：你只需要在第 3 层（App 层）写代码。第 2 层和第 1 层是框架提供的，**不要修改**它们。

### 菜单树是什么？

你在屏幕上看到的菜单，在内存中是一棵**树**：

```
root（隐式根节点，不显示）
├── 设置
│   ├── WiFi（开关）
│   ├── 亮度（滑块）
│   └── 动画效果（开关）
├── 工具
│   └── 秒表 ←── 你即将创建的 App 在这里
└── 关于
```

每个菜单项都是一个**节点**。节点可以有子节点（像文件夹），也可以没有（像文件）。

---

## Step 2：创建 App 的源文件

一个 App 需要两个文件：头文件（`.h`）和实现文件（`.c`）。

### 2.1 创建头文件

在 `src/app/` 目录下新建 `my_stopwatch.h`：

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L215-L228)*

```c
#ifndef MY_STOPWATCH_H
#define MY_STOPWATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 秒表 App 初始化函数
 * @note  进入 App 时调用一次，用于重置状态
 */
void my_stopwatch_init(void *user_data);

/**
 * @brief 秒表 App 主循环函数
 * @note  进入 App 后每帧调用（约 60fps），负责绘制整个屏幕
 */
void my_stopwatch_loop(void *user_data);

/**
 * @brief 秒表 App 退出函数
 * @note  退出 App 时调用一次，用于清理资源
 */
void my_stopwatch_exit(void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MY_STOPWATCH_H */
```

**为什么要用 `extern "C"`**：因为 `main.cpp` 是 C++ 文件，而你的 App 是 C 文件。C++ 编译器会"改写"函数名（Name Mangling），`extern "C"` 告诉编译器"不要改写这些函数名"，这样 C++ 代码才能正确链接到 C 函数。

### 2.2 创建实现文件

在 `src/app/` 目录下新建 `my_stopwatch.c`：

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L170-L211) 生命周期调度*

```c
#include "my_stopwatch.h"

#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "hal/hal_input.h"
#include "ui/ui_item.h"
#include "app/ui_service.h"
#include <stdio.h>

/* ═══ 状态变量 ═══ */

static uint32_t g_start_ms = 0;   /* 计时起始时刻 */
static bool     g_running = false;/* 是否正在计时 */
static uint32_t g_elapsed_at_pause = 0; /* 暂停时已累计时间 */

/* ═══ 生命周期回调 ═══ */

void my_stopwatch_init(void *user_data)
{
    (void)user_data;
    g_start_ms = 0;
    g_running = false;
    g_elapsed_at_pause = 0;
    ui_service_user_item_init();  /* ★ 必须：清除进入前的残留按键事件 */
}

void my_stopwatch_loop(void *user_data)
{
    (void)user_data;

    /* ── 1. 计算已过去的时间 ── */
    uint32_t elapsed = g_elapsed_at_pause;
    if (g_running) {
        elapsed = g_elapsed_at_pause + (hal_get_ticks() - g_start_ms);
    }

    uint32_t seconds = elapsed / 1000;
    uint32_t ms      = elapsed % 1000;

    /* ── 2. 格式化时间字符串 ── */
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu.%03lu",
             seconds / 60, seconds % 60, ms);

    /* ── 3. 计算居中位置并绘制 ── */
    int16_t text_w = hal_get_string_width(buf);
    int16_t text_h = hal_get_font_height();

    int16_t x = (SCREEN_WIDTH  - text_w) / 2;
    int16_t y = (SCREEN_HEIGHT - text_h) / 2;

    hal_draw_string(x, y, buf, COLOR_FG);

    /* ── 4. 绘制操作提示 ── */
    if (g_running) {
        hal_draw_utf8(4, SCREEN_HEIGHT - 10, "B:暂停  A:复位", COLOR_FG);
    } else {
        if (g_elapsed_at_pause == 0 && g_start_ms == 0) {
            hal_draw_utf8(4, SCREEN_HEIGHT - 10, "短按B:开始", COLOR_FG);
        } else {
            hal_draw_utf8(4, SCREEN_HEIGHT - 10, "B:继续  A:复位", COLOR_FG);
        }
    }

    /* ── 5. 读取按键输入 ── */
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);
    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);

    /* ── 6. ★ 标准退出检查（必须在所有 App 输入处理之后）── */
    if (ui_service_user_item_loop(ev_b)) return;

    if (ev_b == HAL_EVENT_SHORT_PRESS) {
        if (!g_running) {
            /* 从未启动过，或暂停后继续 */
            g_start_ms = hal_get_ticks();
            g_running = true;
        } else {
            /* 暂停 */
            g_elapsed_at_pause += hal_get_ticks() - g_start_ms;
            g_running = false;
        }
    }

    if (ev_a == HAL_EVENT_SHORT_PRESS) {
        /* 复位 */
        g_running = false;
        g_start_ms = 0;
        g_elapsed_at_pause = 0;
    }
}

void my_stopwatch_exit(void *user_data)
{
    (void)user_data;
    g_running = false;
    g_start_ms = 0;
    g_elapsed_at_pause = 0;
    ui_service_user_item_exit();  /* ★ 必须：清除退出时的残留事件 */
}
```

**注意**：
- 框架在调用 `loop()` **之前**已经执行了 `hal_display_clear()`，所以你**不需要**在 `loop()` 中手动清屏
- 回调函数必须带有 `void *user_data` 参数（框架会传入 `item->user_data`）
- `ui_service_user_item_init()` 在 `init()` 中调用，`ui_service_user_item_exit()` 在 `exit()` 中调用，否则残留按键会导致意外行为
- `ui_service_user_item_loop(ev_b)` 是标准退出检查，缺少它用户将无法退出 App

### 中文伪代码拆解

```
变量 起始毫秒 = 0
变量 累计毫秒 = 0
变量 正在运行 = false

函数 初始化(user_data) {
    起始毫秒 = 0
    累计毫秒 = 0
    正在运行 = false
    清除按键残留事件()
}

函数 每帧循环(user_data) {
    // 第一步：计算已用时间
    if (正在运行) {
        已过时间 = 累计毫秒 + (当前毫秒() - 起始毫秒)
    } else {
        已过时间 = 累计毫秒
    }

    // 第二步：将毫秒转换为 分:秒.毫秒 格式
    分 = 已过时间 / 60000
    秒 = (已过时间 / 1000) % 60
    毫秒 = 已过时间 % 1000

    // 第三步：在屏幕正中央显示时间
    文字宽度 = 获取文字宽度(格式化后的字符串)
    x = (屏幕宽 - 文字宽度) / 2
    y = (屏幕高 - 字体高) / 2
    绘制字符串(x, y, 字符串, 白色)

    // 第四步：在底部绘制操作提示
    if (正在运行) 提示 "B:暂停  A:复位"
    else if (从未启动过) 提示 "短按B:开始"
    else 提示 "B:继续  A:复位"

    // 第五步：检查按键
    B键事件 = 获取按键事件(B键)
    A键事件 = 获取按键事件(A键)

    // 第六步：标准退出检查
    if (ui_service_user_item_loop(B键事件)) return

    if (B键短按) {
        if (没在运行) { 开始计时 }
        else { 暂停计时 }
    }

    if (A键短按) {
        复位所有状态
    }
}

函数 退出(user_data) {
    正在运行 = false
    起始毫秒 = 0
    累计毫秒 = 0
    ui_service_user_item_exit()
}
```

**核心思想**：`loop` 函数每帧做六件事——计算、格式化、绘制、提示、读按键、检查退出。只要理解了这个循环模式，任何 App 都可以按这个模板写。

---

## Step 3：将 App 注册到菜单树

写好了 App，但它还不会出现在屏幕上。你需要把它"挂"到菜单树上。

### 3.1 包含头文件

打开 `src/app/app_menu.c`，在顶部添加你的头文件：

```c
#include "my_stopwatch.h"
```

### 3.2 在菜单树中创建入口

在 `app_menu_build()` 函数中，添加以下代码：

*📄 Source: [app_menu.c](../../src/app/app_menu.c#L32-L103)*

```c
void app_menu_build(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    /* ... 已有代码保持不变 ... */

    /* ═══ 新增：工具菜单和秒表 App ═══ */
    xerintosh_list_item_t* tools = xerintosh_new_list_item("工具", list_icon);
    xerintosh_push_item_to_list(root, tools);

    xerintosh_list_item_t* stopwatch = xerintosh_new_user_item(
        "秒表",                /* 菜单上显示的名称 */
        my_stopwatch_init,     /* 进入时调用 */
        my_stopwatch_loop,     /* 每帧调用 */
        my_stopwatch_exit,     /* 退出时调用 */
        user_icon              /* 图标 */
    );
    xerintosh_push_item_to_list(tools, stopwatch);
}
```

### 3.3 编译并测试

保存所有文件后，在 VS Code 中点击 PlatformIO 的 **Build** 按钮，或使用命令行：

```bash
pio run
```

如果没有编译错误，上传固件：

```bash
pio run --target upload
```

上传完成后，你的菜单里会多出一个"工具"文件夹，进入后可以看到"秒表"选项。长按 **A 键**（确认键）即可进入。

---

## Step 4：理解生命周期（重要！）

很多初学者第一次写 App 时，不理解框架什么时候调用哪个函数。下面这张图非常重要：

```
用户在菜单中选中"秒表"
        │
        ▼
长按 A 键（确认）
        │
        ▼
框架播放进入动画（约 300ms）
        │
        ▼
调用 my_stopwatch_init()      ←── 只调用一次！
        │
        ▼
进入循环：
    ┌─────────────────────┐
    │ 调用 my_stopwatch_loop() │ ←── 每帧调用，约 60 次/秒
    │       │                │
    │       ▼                │
    │   绘制 → 刷新           │
    │       │                │
    │       ▼                │
    │   用户长按 B 键？       │
    │   否 → 继续循环        │
    │   是 → 退出循环        │
    └─────────────────────┘
        │
        ▼
框架播放退出动画（约 300ms）
        │
        ▼
调用 my_stopwatch_exit()       ←── 只调用一次！
        │
        ▼
回到菜单列表
```

**关键记忆点**：
- `init`：**进入 App 时调用一次**，用于初始化变量、分配资源
- `loop`：**每帧调用**，用于绘制和交互；频率约 60fps
- `exit`：**退出 App 时调用一次**，用于清理资源、保存状态

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L170-L211)*

---

## Step 5：可用的绘制工具箱

在 `loop` 函数中，你可以使用以下 HAL API 绘制界面。

### 基础形状

```c
/* 像素和线段 */
hal_draw_pixel(10, 20, COLOR_FG);              /* 在 (10,20) 画一个白点 */
hal_draw_line(0, 0, 80, 160, COLOR_FG);        /* 从左上角画到右下角 */
hal_draw_h_line(10, 30, 50, COLOR_FG);         /* 从 (10,30) 向右画 50 像素横线 */
hal_draw_v_line(10, 30, 40, COLOR_FG);         /* 从 (10,30) 向下画 40 像素竖线 */

/* 矩形 */
hal_draw_rect(5, 5, 70, 30, COLOR_FG);         /* 空心矩形 */
hal_draw_fill_rect(5, 5, 70, 30, COLOR_FG);    /* 实心矩形（白色填充） */
hal_draw_round_rect(5, 5, 70, 30, 4, COLOR_FG);/* 圆角矩形，半径 4 */

/* 圆形 */
hal_draw_circle(40, 80, 20, COLOR_FG);         /* 空心圆，圆心 (40,80)，半径 20 */
```

### 文字

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L164-L170)*

```c
/* 绘制英文/数字/中文 */
hal_draw_string(10, 20, "Hello", COLOR_FG);
hal_draw_utf8(10, 40, "你好", COLOR_FG);       /* hal_draw_utf8 是 hal_draw_string 的宏别名 */

/* 设置字体（如需使用自定义字体） */
hal_set_font(hal_get_cn_font());   /* 使用内置中文字体 */

/* 获取文字尺寸（用于居中计算） */
int16_t w = hal_get_string_width("Hello");
int16_t h = hal_get_font_height();
```

### 屏幕操作

```c
/* 框架每帧已自动调用 hal_display_clear()，你不需要手动调用 */
hal_display_flush();     /* 刷新到屏幕 —— 框架会自动调用，你不需要手动调用 */
```

### 颜色常量

| 常量 | 值 | 颜色 |
|------|-----|------|
| `COLOR_BG` | `0x0000` | 黑色 |
| `COLOR_FG` | `0xFFFF` | 白色 |
| `COLOR_ACCENT` | `0x07E0` | 绿色 |

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L31-L34)*

你也可以使用任意 RGB565 颜色值，例如 `0xF800` 是红色，`0x001F` 是蓝色。

### 位图

```c
/* 绘制 XBM 格式的位图 */
static const uint8_t my_icon[] = { 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };
hal_draw_xbitmap(10, 10, 8, 8, my_icon);
```

---

## Step 6：处理按键输入

M5Stick-C 有两个按键：

| 按键 | 位置 | 短按 | 长按（≥500ms） |
|------|------|------|----------------|
| **Btn A** | 侧面小按钮 | 菜单中：下移一项 | 确认/进入 |
| **Btn B** | 正面大按钮 | 菜单中：上移一项 | 返回/退出 |

*📄 Source: [app_init.c](../../src/app/app_init.c#L469-L494)*

在 `user_item` 的 `loop` 函数中，框架**不再自动处理**按键导航，你需要自己读取按键事件。

### 读取事件（推荐方式）

```c
#include "hal/hal_input.h"

void my_app_loop(void *user_data)
{
    (void)user_data;

    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);

    /* ★ 标准退出检查（必须在所有 App 输入处理之后）*/
    if (ui_user_item_try_exit(ev_b)) return;

    if (ev_a == HAL_EVENT_SHORT_PRESS) {
        /* A 键短按 */
    }
    if (ev_a == HAL_EVENT_LONG_PRESS) {
        /* A 键长按 */
    }

    if (ev_b == HAL_EVENT_SHORT_PRESS) {
        /* B 键短按 */
    }
}
```

**⚠️ 重要**：在 `app_init.c` 的 `app_input_process()` 中，框架每帧都会调用 `hal_input_update()`，但当处于 `user_item` 内部时会立即返回，不处理框架导航。你的 App 需要通过 `ui_user_item_try_exit()` 来响应长按 B 的退出请求。这意味着：
- **长按 B 键 = 退出 App** 是框架的默认行为
- 短按 A/B、长按 A 可以自由使用
- 如果你的 App 需要用到长按 B（比如游戏中的"射击"），你需要在 `app_init.c` 中添加一个全局标志来临时禁用框架导航

### 查询持续按下的时间

```c
uint32_t dur = hal_input_get_press_duration(HAL_BTN_A);
if (dur > 0) {
    /* A 键正被按住，dur 是已按住的毫秒数 */
}
```

---

## Step 7：使用框架通知

即使在 `user_item` 内部，你也可以调用框架的通知 API：

```c
#include "ui/ui_item.h"

/* 顶部信息栏（自动收回，适合简短提示） */
xerintosh_push_info_bar("已保存", 1500);   /* 显示 1.5 秒 */

/* 中部弹窗（更显眼，适合重要提示） */
xerintosh_push_pop_up("计时结束！", 2000); /* 显示 2 秒 */
```

信息栏和弹窗会在你的 `loop` 绘制**之上**叠加显示，由框架自动处理动画和收回。

---

## 完整示例：秒表 App（可直接复制使用）

以下是经过整理的完整代码，你可以直接复制到项目中编译运行：

### my_stopwatch.h

```c
#ifndef MY_STOPWATCH_H
#define MY_STOPWATCH_H

#ifdef __cplusplus
extern "C" {
#endif

void my_stopwatch_init(void *user_data);
void my_stopwatch_loop(void *user_data);
void my_stopwatch_exit(void *user_data);

#ifdef __cplusplus
}
#endif

#endif
```

### my_stopwatch.c

```c
#include "my_stopwatch.h"
#include "hal/hal_display.h"
#include "hal/hal_system.h"
#include "hal/hal_input.h"
#include "ui/ui_item.h"
#include <stdio.h>

static uint32_t g_start_ms = 0;
static bool     g_running = false;
static uint32_t g_elapsed_at_pause = 0;

void my_stopwatch_init(void *user_data)
{
    (void)user_data;
    g_start_ms = 0;
    g_running = false;
    g_elapsed_at_pause = 0;
    hal_input_reset_events();
}

void my_stopwatch_loop(void *user_data)
{
    (void)user_data;

    /* 计算已用时间 */
    uint32_t elapsed = g_elapsed_at_pause;
    if (g_running) {
        elapsed = g_elapsed_at_pause + (hal_get_ticks() - g_start_ms);
    }

    uint32_t minutes = elapsed / 60000;
    uint32_t seconds = (elapsed / 1000) % 60;
    uint32_t ms      = elapsed % 1000;

    /* 主时间显示（大号） */
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", minutes, seconds);

    int16_t tw = hal_get_string_width(buf);
    int16_t th = hal_get_font_height();
    hal_draw_string((SCREEN_WIDTH - tw) / 2, (SCREEN_HEIGHT - th) / 2 - 10, buf, COLOR_FG);

    /* 毫秒（小号，在时间下方） */
    char ms_buf[8];
    snprintf(ms_buf, sizeof(ms_buf), ".%03lu", ms);
    int16_t msw = hal_get_string_width(ms_buf);
    hal_draw_string((SCREEN_WIDTH - msw) / 2, (SCREEN_HEIGHT - th) / 2 + 8, ms_buf, COLOR_FG);

    /* 分隔线 */
    hal_draw_h_line(10, SCREEN_HEIGHT / 2 + 20, SCREEN_WIDTH - 20, COLOR_FG);

    /* 状态提示 */
    if (g_running) {
        hal_draw_utf8(4, SCREEN_HEIGHT - 12, "● 运行中", COLOR_FG);
    } else if (g_elapsed_at_pause > 0) {
        hal_draw_utf8(4, SCREEN_HEIGHT - 12, "■ 已暂停", COLOR_FG);
    } else {
        hal_draw_utf8(4, SCREEN_HEIGHT - 12, "○ 准备就绪", COLOR_FG);
    }

    /* 操作提示 */
    hal_draw_utf8(SCREEN_WIDTH - 50, SCREEN_HEIGHT - 12,
                  g_running ? "B暂停 A复位" : "B开始 A复位", COLOR_FG);

    /* 按键处理 */
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);
    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);

    /* 标准退出检查 */
    if (ui_user_item_try_exit(ev_b)) return;

    if (ev_b == HAL_EVENT_SHORT_PRESS) {
        if (!g_running) {
            g_start_ms = hal_get_ticks();
            g_running = true;
        } else {
            g_elapsed_at_pause += hal_get_ticks() - g_start_ms;
            g_running = false;
        }
    }

    if (ev_a == HAL_EVENT_SHORT_PRESS) {
        g_running = false;
        g_start_ms = 0;
        g_elapsed_at_pause = 0;
    }
}

void my_stopwatch_exit(void *user_data)
{
    (void)user_data;
    g_running = false;
    g_start_ms = 0;
    g_elapsed_at_pause = 0;
    hal_input_reset_events();
}
```

### 注册到菜单（在 app_init.c 中）

```c
#include "my_stopwatch.h"

void app_init_ui(void)
{
    xerintosh_list_item_t* root = xerintosh_get_root_list();

    /* ... 已有代码 ... */

    xerintosh_list_item_t* tools = xerintosh_new_list_item("工具", list_icon);
    xerintosh_push_item_to_list(root, tools);

    xerintosh_push_item_to_list(tools, xerintosh_new_user_item(
        "秒表",
        my_stopwatch_init,
        my_stopwatch_loop,
        my_stopwatch_exit,
        user_icon));
}
```

---

## 常见问题排查

### Q1：编译报错 `undefined reference to 'my_stopwatch_init'`

**原因**：PlatformIO 不知道要编译你的新文件。

**解决**：打开 `platformio.ini`，确保 `src_filter` 或 `build_src_filter` 包含了 `src/app/` 目录下的所有 `.c` 文件。默认情况下，PlatformIO 会自动编译 `src/` 下的所有文件，但如果你的配置文件有自定义过滤规则，需要手动添加。

### Q2：进入 App 后屏幕是黑的，什么显示都没有

**原因**：没有在 `loop` 函数中调用绘制函数。

**解决**：确保 `loop` 函数中有绘制代码。注意：**框架已在调用 `loop()` 前自动执行了 `hal_display_clear()`**，你不需要自己清屏，只需要绘制内容即可。

### Q3：文字显示为乱码

**原因**：使用了不支持中文的字体，或字符串编码不正确。

**解决**：中文必须使用 `hal_draw_utf8()`（实际上是 `hal_draw_string()` 的宏别名）。确保源文件保存为 UTF-8 编码。

### Q4：按键没有反应

**原因 1**：没有读取按键事件。

**解决**：在 `loop` 函数中调用 `hal_input_get_event(HAL_BTN_X)` 读取事件。

**原因 2**：没有调用 `ui_user_item_try_exit()`，但 App 也无法退出。这不是"没反应"，而是输入处理逻辑有问题。

**解决**：检查你的按键处理逻辑是否正确。

**原因 3**：框架的长按 B 退出逻辑和你的按键处理冲突了。

**解决**：短按 A/B 和长按 A 可以自由使用。长按 B 默认是退出 App，如果你需要用到长按 B，参考 [开发者指南](../developer-guide.md) 第 4.3 节，通过全局标志让 `app_input_process()` 跳过导航。

### Q5：App 闪退或重启

**原因**：栈溢出或空指针。

**解决**：
- 检查 `snprintf` 的目标缓冲区是否足够大
- 检查是否访问了 NULL 指针
- 检查数组是否越界
- 在 `loop` 中不要使用大型局部数组（ESP32 的栈空间有限）

### Q6：我想在 App 中绘制更复杂的图形

**解决**：使用 `hal_draw_*` 系列函数组合。例如，要绘制一个进度条：

```c
void draw_progress_bar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t percent)
{
    if (w <= 2 || h <= 2) return;  /* 尺寸不足时无法绘制内边距 */
    /* 外框 */
    hal_draw_rect(x, y, w, h, COLOR_FG);
    /* 填充（裁剪 percent 到 0~100，避免越界绘制） */
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int16_t fill_w = (w - 2) * percent / 100;
    if (fill_w > 0) {
        hal_draw_fill_rect(x + 1, y + 1, fill_w, h - 2, COLOR_FG);
    }
}
```

---

## 下一步

掌握了基础后，你可以尝试：

1. **读取传感器数据**：在 `init` 中初始化传感器，在 `loop` 中读取并显示
2. **添加设置项**：在菜单中添加 `slider_item` 或 `switch_item` 来控制 App 参数
3. **绘制动画**：利用 `hal_get_ticks()` 计算时间，制作简单的帧动画
4. **保存状态**：使用 `storage.cpp` 中的 NVS API，在 `exit` 中保存，在 `init` 中恢复

更多高级用法请参考 [开发者指南](../developer-guide.md)。

---

> **See Also:** [开发者指南](../developer-guide.md) | [项目系统](../ui/item.md) | [显示驱动](../hal/display.md) | [输入系统](../hal/input.md)
