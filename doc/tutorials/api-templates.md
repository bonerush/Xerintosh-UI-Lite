# API 调用模板与常见陷阱

> **Parent:** [知识地图](../index.md) | **Related:** [从零开始创建 App](your-first-app.md), [项目系统](../ui/item.md), [编码风格规范](../coding-style.md)
>
> 本文档为 AI 和人类开发者提供**可直接复制的 API 调用模板**。每个模板包含完整代码示例、参数说明、生命周期规则和常见陷阱。

---

## 概述

本框架使用 **C 风格面向对象** 设计，所有 UI 组件共享统一的回调签名和内存管理规则。常见的 Bug 来源于：

1. **绑定的变量生命周期过短**（局部变量地址传给框架后失效）
2. **user_data 内存管理遗漏**（动态分配但未在 destroy 回调中释放）
3. **线程上下文混淆**（在错误的任务中调用底层 API）

**核心规则（记住这两条就能避免 80% 的 Bug）：**

> **规则 1：传给框架的指针必须永久有效** — `switch_item.value`、`slider_item.value` 等必须指向 `static` 或全局变量，**严禁**指向局部变量。
>
> **规则 2：user_data 的动态内存由你自己管理** — 若 `user_data` 指向 `malloc` 内存，必须设置 `destroy_callback` 释放。

---

## 第一部分：UI 组件调用模板

### 统一回调签名

所有回调使用相同的函数指针类型：

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L51)*
```c
typedef void (*xerintosh_cb_t)(void *user_data);
```

- `user_data` 来自 `item->user_data` 字段
- 回调中通过 `user_data` 获取上下文，或通过 `item->user_data`（从选中项取）

---

### 模板 1：`list_item` — 子菜单容器

**用途**：创建子菜单、作为其他 item 的容器。

*📄 Source: [app_init.c](../../src/app/app_init.c#L151-L152)*
```c
/* 1. 创建父容器 */
xerintosh_list_item_t *parent = xerintosh_new_list_item("设置", list_icon);

/* 2. 创建子项 */
xerintosh_list_item_t *child = xerintosh_new_switch_item(
    "WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);

/* 3. 挂载子项到父容器（顺序决定菜单顺序）*/
xerintosh_push_item_to_list(parent, child);
```

| 参数 | 说明 |
|------|------|
| `content` | 显示文本，内部 `strdup` 复制，原始字符串可释放 |
| `icon` | 图标枚举。`default_icon` 自动降级为 `list_icon` |

**特殊用法 — `init_function` 动态初始化子菜单**：

*📄 Source: [app_init.c](../../src/app/app_init.c#L207)*
```c
/* 进入子菜单时自动调用，常用于动态更新子项内容 */
xerintosh_list_item_t *submenu = xerintosh_new_list_item("烧录器引脚", default_icon);
submenu->init_function = on_enter_flasher_submenu;
```

---

### 模板 2：`switch_item` — 开关项

**用途**：绑定一个 `bool` 变量，确认时翻转。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L179-L181)*
```c
/* ★ 变量必须是 static 或全局的！*/
static bool g_my_feature_enabled = false;

/* 创建开关项 */
xerintosh_list_item_t *sw = xerintosh_new_switch_item(
    "我的功能",          // 显示文本
    &g_my_feature_enabled,// ★ 指向永久变量的指针
    NULL,                 // init_function（进入时回调，可选）
    on_switch_changed,    // exit_function（值改变后回调，可选）
    default_icon
);

/* 回调示例 */
static void on_switch_changed(void *user_data) {
    (void)user_data;
    // g_my_feature_enabled 已被框架翻转
    if (g_my_feature_enabled) {
        /* 启用功能 */
    } else {
        /* 禁用功能 */
    }
}
```

**⚠️ 陷阱**：`value` 指针传入后框架只读写其指向的值，不管理指针生命周期。如果指向局部变量，出作用域后行为未定义。

**⚠️ 框架缺陷**：`xerintosh_new_switch_item()` 内部不检查 `_value` 是否为 NULL（[ui_item_base.c:121](../../src/ui/ui_item_base.c#L121) 直接赋值）。传入 NULL 会在用户确认此项时导致空指针解引用崩溃。

---

### 模板 3：`slider_item` — 滑块项

**用途**：绑定一个 `int16_t` 变量，两次确认修改数值。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L209-L212)*
```c
/* ★ 变量必须是 static 或全局的！*/
static int16_t g_my_slider_value = 5;

xerintosh_list_item_t *sl = xerintosh_new_slider_item(
    "亮度",                // 显示文本
    &g_my_slider_value,    // ★ 指向永久变量的指针
    1,                     // step：每次增减的步进值
    1,                     // min：最小值
    10,                    // max：最大值
    NULL,                  // init_function（可选）
    on_slider_changed,     // exit_function（确认后回调）
    default_icon
);

static void on_slider_changed(void *user_data) {
    (void)user_data;
    // g_my_slider_value 已被框架更新为确认后的值
    apply_new_value(g_my_slider_value);
}
```

**操作逻辑**：
1. 选中 + **长按 A 确认** → **进入编辑模式**（上下键变为增减数值）
2. 再按 **长按 A 确认** → **保存修改**，触发 `exit_function`
3. **长按 B 返回** → **取消修改**，恢复原值

*📄 Source: [app_init.c](../../src/app/app_init.c#L469-L494)*

**⚠️ 陷阱**：编辑模式下上下键不再是导航，而是增减数值（A 增加，B 减少）。调用方不需要额外处理取消逻辑（框架自动恢复备份值）。

**⚠️ 编辑模式下的并发风险**：编辑期间 `*value` 处于中间"脏"状态——每次按键都直接修改绑定的全局变量。如果另一段代码在此时读取该变量，会看到**未确认的中间值**。应仅在 `exit_function` 回调或确认后读取 slider 变量的值。

---

### 模板 4：`button_item` — 按钮项

**用途**：单次触发回调，无需绑定变量。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L192-L193)*
```c
xerintosh_list_item_t *btn = xerintosh_new_button_item(
    "执行操作",         // 显示文本
    on_button_pressed,  // 按下时触发的回调
    default_icon
);

/* 传递整数参数的标准模式 */
btn->user_data = (void*)(intptr_t)42;

static void on_button_pressed(void *user_data) {
    int value = (int)(intptr_t)user_data;
    // 使用 value...
}
```

**⚠️ 陷阱 — 按钮回调中创建弹窗的安全做法**：

*📄 Source: [app_init.c](../../src/app/app_init.c#L90-L93)*
```c
/*
 * 按钮回调中不宜直接调用 xerintosh_push_pop_up() 或 M5GFX 绘制函数：
 * 如果回调在 Xeros 内核调度上下文中执行，M5GFX 的 FreeRTOS 信号量
 * 可能不可用，触发 task timeout。
 * 安全做法：设标志位，由主循环的 app_input_process() 统一处理。
 * （如果在 UI 任务上下文中的回调则通常安全，但为一致性仍推荐延迟模式）
 */
static bool g_deferred_popup_pending = false;

static void on_button_pressed(void *user_data) {
    (void)user_data;
    g_deferred_popup_pending = true;  // 只设标志位
}

/* 在 app_input_process() 中每帧检查 */
void app_input_process(void) {
    if (g_deferred_popup_pending) {
        g_deferred_popup_pending = false;
        xerintosh_push_pop_up("操作成功！", 2000);  // 在主循环中安全调用
    }
}
```

---

### 模板 5：`user_item` — 全屏 App 入口

**用途**：创建独立的交互式 App（任务管理器、串口监视器等）。

*📄 Source: [ui_item_core.h](../../src/ui/ui_item_core.h#L226-L228)*
```c
xerintosh_list_item_t *app = xerintosh_new_user_item(
    "我的 App",    // 显示文本
    my_app_init,   // init_function：进入时调用一次
    my_app_loop,   // loop_function：每帧调用
    my_app_exit,   // exit_function：退出时调用一次
    user_icon
);

/* —— App 三个生命周期函数的标准模板 —— */

static void my_app_init(void *user_data) {
    (void)user_data;
    hal_input_reset_events();  // ★ 必须：清除进入前的残留按键事件
    // 初始化你的 App 状态...
}

static void my_app_loop(void *user_data) {
    (void)user_data;

    /* 1. 处理输入 */
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);
    hal_event_t event_a = hal_input_get_event(HAL_BTN_A);

    /* 2. ★ 标准退出检查（必须在所有 App 输入处理之后）*/
    if (ui_user_item_try_exit(event_b)) return;

    /* 3. 你的业务逻辑... */

    /* 4. 渲染（框架在调用 loop 前已自动 hal_display_clear）*/
    hal_draw_string(5, 5, "Hello World!", COLOR_FG);
}

static void my_app_exit(void *user_data) {
    (void)user_data;
    hal_input_reset_events();  // ★ 必须：清除退出时的残留事件
    // 清理你的 App 状态...
}
```

**生命周期图示**：
```
选中 → 长按 A 确认 → init() 调用一次
                → loop() 每帧调用（直到退出）
                → 长按 B → exit() 调用一次 → 回到菜单
```

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L170-L211)*

**⚠️ 陷阱**：

1. **不要在 `loop()` 内调用 `hal_display_clear()`**：框架的 `ui_task` 会在每帧调用 `loop()` 之前自动清屏。重复清屏不会出错，但不必要。

2. **`destroy_callback` 需要手动设置**：若 `user_data` 是动态分配的，必须设置 `destroy_callback` 才能正确释放：
   ```c
   my_app->user_data = malloc(sizeof(my_state_t));
   ((xerintosh_user_item_t*)my_app)->destroy_callback = my_app_destroy;
   
   static void my_app_destroy(void *user_data) {
       free(user_data);
   }
   ```

3. **`init()` 中必须调用 `hal_input_reset_events()`**：否则确认进入 App 的长按事件会被 `loop()` 的第一帧消费，导致意外行为。

4. **`ui_user_item_try_exit()` 内部有防重复 guard**：除了检查 `HAL_EVENT_LONG_PRESS`，还检查 `!exiting_user_item` 防止重复触发退出流程。

---

### 模板 6：弹窗（Pop-up）

**用途**：在屏幕中部显示短暂消息。

*📄 Source: [ui_widget.h](../../src/ui/ui_widget.h#L76)*
```c
// 弹窗 API 通过 ui_item.h 提供（无需单独 include）

/* 基本用法 — 显示 2 秒后自动消失 */
xerintosh_push_pop_up("操作完成！", 2000);

/* 手动控制生命周期 */
xerintosh_push_pop_up("正在处理...", 0);     // duration=0 表示不自动消失
// ... 做一些事情 ...
xerintosh_push_pop_up("处理完成！", 2000);    // 新的弹窗会覆盖旧的
xerintosh_hide_pop_up();                     // 立即隐藏（无动画）
xerintosh_dismiss_pop_up();                  // 动画退出（向上滑出）
```

**⚠️ 陷阱**：

- **不能每帧 dismiss**：`xerintosh_dismiss_pop_up()` 只在确定要关闭时调用一次。每帧调用会误杀其他模块创建的弹窗。
- **弹窗是全局单例**：同一时间只有一个弹窗。新的弹窗会覆盖旧的。
- **每帧 push 保持弹窗常驻**：`duration=0` 的弹窗不会自动消失。如需跨帧保持显示（如 WiFi 模块在独立任务中维持弹窗），需每帧调用 `xerintosh_push_pop_up()` 重置计时器。这是有意为之的设计模式，由各模块自行管理超时。
- **按钮回调中不能创建弹窗**：见模板 4 的陷阱说明。

---

## 第二部分：服务模块调用模板

### 模板 7：WiFi 管理器

**用途**：控制 WiFi 开关、扫描连接网络。

*📄 Source: [wifi_manager.h](../../src/app/wifi/wifi_manager.h)*
```c
#include "wifi/wifi_manager.h"

/* —— 初始化（由 app_init 在启动时自动调用）—— */
// wifi_mgr_init();

/* —— 开关控制模式 —— */
/* 1. 通过菜单开关控制（推荐）*/
// 在 app_init.c 中创建 switch_item：
xerintosh_new_switch_item("WiFi", &g_wifi_on, NULL, wifi_mgr_on_switch_toggle, default_icon);

/* 2. 程序化控制 */
wifi_mgr_enable();   // 启用 WiFi（进入扫描状态）
wifi_mgr_disable();  // 禁用 WiFi（完全释放驱动）

/* —— 状态查询 —— */
bool is_on   = wifi_mgr_is_enabled();
bool waiting = wifi_mgr_is_waiting_input();  // 是否在等待用户输入密码

/* —— 连接流程 —— */
/*
 * 1. wifi_mgr_init() → 系统启动时调用一次
 * 2. wifi_mgr_enable() → 进入 WARMUP → SCANNING → SCAN_DONE
 * 3. wifi_mgr_update() → 由独立内核任务每 50ms 轮询（wifi_mgr_task_main）
 * 4. 用户在串口输入密码 → serial_input 模块接收 → CONNECTING → CONNECTED/FAILED
 *
 * 注意：WiFi 扫描和连接在独立内核任务中异步进行，
 * UI 不会被阻塞。密码输入通过串口 CLI 处理。
 */
```

**⚠️ 陷阱**：
- `wifi_mgr_disable()` 会完全释放约 34-38KB WiFi 驱动内存（取决于 ESP-IDF 版本），下次 `enable` 需要重新初始化
- WiFi 弹窗刷新在 UI 任务的 `app_input_process()` 中执行

---

### 模板 8：蓝牙管理器

**用途**：控制 Classic Bluetooth SPP 串口连接。

*📄 Source: [bt_manager.h](../../src/app/bluetooth/bt_manager.h) | [bt_uart_service.h](../../src/app/bluetooth/bt_uart_service.h)*
```c
#include "bluetooth/bt_manager.h"
#include "bluetooth/bt_uart_service.h"

/* —— 初始化（由 app_init 在启动时自动调用）—— */
// bt_mgr_init();

/* —— 开关控制模式 —— */
/* 1. 通过菜单开关控制（推荐）*/
xerintosh_new_switch_item("蓝牙", &g_bt_on, NULL, bt_mgr_on_switch_toggle, default_icon);

/* 2. 程序化控制（异步 API，可从任意任务安全调用）*/
bt_mgr_request_enable();   // ★ 设置 volatile 标志，实际启用由 bt_mgr_process_requests() 执行
bt_mgr_request_disable();  // ★ 同上

/* 3. 必须循环调用（在 Arduino loop() 中）*/
bt_mgr_process_requests();  // 处理待处理请求，执行实际的 BluetoothSerial 操作

/* —— 状态查询 —— */
bool is_on      = bt_mgr_is_enabled();
bool connected  = bt_uart_is_connected();
bool waiting    = bt_mgr_is_waiting_input();

/* —— 状态机更新（由独立内核任务每 50ms 轮询）—— */
// bt_mgr_update() → 检测 bt_uart_is_connected() 状态变化，更新 bt_mgr_state_t

/* —— RX 数据回调 —— */
void my_bt_rx_handler(const uint8_t *data, uint16_t len) {
    // 处理接收到的数据...
}
bt_uart_set_rx_callback(my_bt_rx_handler);

/* —— 连接状态回调 —— */
void my_bt_connect_handler(bool connected) {
    // 连接/断开通知...
}
bt_uart_set_connect_callback(my_bt_connect_handler);

/* —— 数据收发 —— */
bt_uart_send_string("Hello from M5Stick!\r\n");
uint8_t buf[] = {0x01, 0x02, 0x03};
bt_uart_send(buf, sizeof(buf));

/* —— RX 队列消费（在 UI 任务中每帧调用）—— */
bt_uart_drain_rx_queue();  // 从 FreeRTOS Queue 读取数据并触发 RX 回调

/* —— 注意：bt_uart_poll() 在 Arduino loop() 中每 50ms 调用 —— */
// 因为 BluetoothSerial.read()/connected() 必须与 begin() 在同一任务上下文
// bt_uart_poll() 不在 bt_mgr_task_main 内核任务中，而在 main.cpp loop() 内
```

**⚠️ 关键架构约束**：

```
[任意任务] → bt_mgr_request_enable() → 设置 volatile 标志
     ↓
[Arduino loop()] → bt_mgr_process_requests() → bt_mgr_enable()
     ↓
[同一任务上下文] → BluetoothSerial::begin()
```

- `BluetoothSerial` 的所有操作（`begin`/`read`/`write`）**必须在同一 FreeRTOS 任务中**
- 从 **UI 任务** 调用 BT 时，必须使用异步请求 API（`bt_mgr_request_enable/disable`）
- 从 **BT 任务** 调用 UI 相关操作也需要异步

**⚠️ WiFi/BT 互斥**：
- 启用 BT 前会自动关闭 WiFi（`wifi_mgr_disable()` + `delay(500)` 等待内存释放）
- 禁用 BT 后会自动恢复此前关闭的 WiFi
- 堆内存需求：BT 最小堆要求 70KB，实际初始化约需 92KB

---

### 模板 9：NVS 存储

**用途**：持久化保存配置到 NVS 闪存。

*📄 Source: [storage.h](../../src/app/storage/storage.h)*
```c
#include "storage/storage.h"

/* —— 初始化（只需一次）—— */
/* storage_init() 由 main.cpp 的 setup() 在系统启动时调用 */

/* —— 读写整数值 —— */
int16_t brightness = storage_get_brightness();    // 读取（未设置返回 -1）
storage_set_brightness(8);                        // 写入

uint8_t speed = storage_get_anim_speed();         // 默认 92
storage_set_anim_speed(92);

bool anim_on = storage_get_anim_enabled();        // 默认 true
storage_set_anim_enabled(false);

/* —— 系统设置 —— */
uint8_t rot = storage_get_screen_rotation();      // 屏幕方向（1=竖屏, 2=横屏）
storage_set_screen_rotation(2);
int16_t baud = storage_get_serial_baud_rate();    // 串口波特率等级（默认 5=115200）
storage_set_serial_baud_rate(5);

/* —— 读写字符串 —— */
char api_key[128];
if (storage_get_deepseek_key(api_key, sizeof(api_key))) {
    // api_key 有效
}
storage_set_deepseek_key("sk-xxxx");

/* —— 凭据操作（WiFi 示例，BT 同构）—— */
int count = storage_wifi_get_count();
for (int i = 0; i < count; i++) {
    char ssid[33], pass[65];
    if (storage_wifi_get(i, ssid, pass)) {
        // 使用凭据...
    }
}
storage_wifi_add("MyNetwork", "password123");
int idx = storage_wifi_find("MyNetwork");           // 按 SSID 查找索引
storage_wifi_remove(0);                             // 按索引删除
```

**⚠️ 陷阱**：
- 每个函数内部 `prefs.begin/end` 成对使用，不能跨函数持有 handle
- NVS key 使用版本化命名（如 `serial_baud_v1`），更改默认值时需改 key 名，否则旧设备不会更新
- 字符串缓冲区需要调用方提供，注意 `max_len` 参数防止溢出

---

### 模板 10：串口输入 CLI

**用途**：通过串口获取用户文本输入（WiFi 密码等）。

*📄 Source: [serial_input.h](../../src/app/serial_input/serial_input.h)*
```c
#include "serial_input/serial_input.h"

/* —— 请求输入 —— */
serial_request_wifi_password("MyWiFiSSID");

/* —— 检查是否在等待输入（用于 dev_ttyS0_poll 判断串口字符归属）—— */
if (serial_input_is_waiting()) {
    // 串口字符被 serial_input 接管，不转发给 shell
}

/* —— 轮询状态（在 UI 任务中每帧调用）—— */
serial_state_t state = serial_poll();
switch (state) {
case SERIAL_STATE_PASSWORD_RECEIVED: {
    const char *password = serial_get_input();    // 获取输入
    const char *target   = serial_get_target_name();
    // 使用 password...
    // ★ 一次性消费：serial_get_input() 下次调用返回 NULL
    break;
}
case SERIAL_STATE_CANCELLED:
    // 用户取消或 30 秒超时
    break;
case SERIAL_STATE_WAITING_PASSWORD:
    // 等待输入中...
    break;
default:
    break;
}

/* —— 取消等待 —— */
serial_cancel();
```

**⚠️ 陷阱**：
- `serial_get_input()` 只返回一次有效值，消费后自动标记已处理
- 密码回显为 `*` 掩码，不泄露到串口
- 30 秒超时自动取消
- 在等待输入期间，`dev_ttyS0_poll()` 会将串口字符交给 `serial_input` 而非 shell

---

### 模板 11：HAL 显示 API

**用途**：在屏幕上绘制文字和图形。

*📄 Source: [hal_display.h](../../src/hal/hal_display.h)*
```c
#include "hal_display.h"
#include "hal_layout.h"

/* —— 文字绘制 —— */
hal_set_font(hal_get_cn_font());                     // 设置中文字体
hal_draw_string(5, 10, "Hello", COLOR_FG);            // x, y, text, color（4参数）
hal_draw_utf8(5, 10, "你好", COLOR_FG);               // UTF-8 别名（同 hal_draw_string）
int16_t tw = hal_get_string_width("Hello");           // 获取字符串宽度（像素）
int16_t fh = hal_get_font_height();                   // 获取当前字体高度

/* —— 基本图形 —— */
hal_draw_pixel(10, 20, COLOR_FG);
hal_draw_line(0, 0, 80, 160, COLOR_FG);
hal_draw_rect(5, 5, 70, 150, COLOR_FG);              // 空心矩形
hal_draw_fill_rect(5, 5, 70, 150, COLOR_ACCENT);     // 实心矩形
hal_draw_round_rect(3, 3, 74, 154, 4, COLOR_FG);     // 空心圆角矩形（r=圆角半径）
hal_draw_fill_round_rect(3, 3, 74, 154, 4, COLOR_FG); // 实心圆角矩形
hal_draw_circle(40, 80, 10, COLOR_RED);              // 空心圆（cx, cy, r, color）
hal_draw_h_line(5, 100, 70, COLOR_FG);               // 水平线段
hal_draw_v_line(40, 50, 60, COLOR_FG);               // 垂直线段
hal_draw_xor_rect(5, 5, 70, 20);                     // XOR 反色矩形（选择器高亮）

/* —— 图标绘制（XBM 位图）—— */
extern const uint8_t my_icon_bits[];                   // XBM 格式位图数据
hal_draw_xbitmap(10, 20, 16, 16, my_icon_bits);       // x, y, w, h, bitmap

/* —— 裁剪区域 —— */
hal_set_clip_rect(5, 5, 70, 150);                     // 后续绘制仅限于此矩形
hal_clear_clip_rect();                                // 恢复全屏绘制

/* —— 颜色常量（RGB565）—— */
// COLOR_BG      = 0x0000  黑色（背景色）
// COLOR_FG      = 0xFFFF  白色（前景色）
// COLOR_ACCENT  = 0x07E0  绿色（强调色）
// COLOR_RED     = 0xF800  红色

/* —— 布局工具宏（hal_layout.h）—— */
int16_t w  = SCREEN_WIDTH;          // 屏幕宽度（横屏=160, 竖屏=80）
int16_t h  = SCREEN_HEIGHT;         // 屏幕高度
int16_t x  = HAL_CENTER_X(tw);      // 使宽度为 tw 的元素水平居中
int16_t y  = HAL_CENTER_Y(fh);      // 使高度为 fh 的元素垂直居中
int16_t lx = HAL_LEFT_X();          // 左对齐坐标（= HAL_MARGIN_MD）
int16_t bl = HAL_TEXT_BASELINE(row_top); // 文字基线（在行顶部 y 处）
int16_t row_h = HAL_ROW_H();        // 标准行高（= 字体高度 + 2*小边距）

// 原始边距常量：HAL_MARGIN_SM(2), HAL_MARGIN_MD(4), HAL_MARGIN_LG(8)
```

**⚠️ 陷阱**：
- `hal_draw_string()` 只有 4 个参数（`x, y, str, color`），无背景色参数
- `hal_draw_xbitmap()` 只有 5 个参数（`x, y, w, h, bitmap`），无颜色参数
- `hal_set_font(NULL)` 重置字体为默认，App 退出前应恢复
- 框架每帧自动 `hal_display_clear()` + `hal_display_flush()`，App 的 `loop()` 中不需要手动调用
- `hal_draw_rect` 和 `hal_draw_fill_rect` 是两个独立函数（非 fill 参数模式）

---

### 模板 12：HAL 输入 API

**用途**：读取按键事件。

*📄 Source: [hal_input.h](../../src/hal/hal_input.h)*
```c
#include "hal_input.h"

/* —— 读取事件（在 loop() 中每帧调用）—— */
hal_event_t event_a = hal_input_get_event(HAL_BTN_A);
hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

/* —— 标准输入处理模式 —— */
switch (event_a) {
case HAL_EVENT_SHORT_PRESS:    // 短按（≥50ms, <500ms）
    do_short_action();
    break;
case HAL_EVENT_LONG_PRESS:     // 长按（≥500ms）
    do_long_action();
    break;
case HAL_EVENT_DOUBLE_CLICK:   // 双击
    do_double_action();
    break;
case HAL_EVENT_NONE:
default:
    break;
}

/* —— 其他辅助 API —— */
bool pressed = hal_input_is_pressed(HAL_BTN_A);       // 查询按键是否按下（实时状态）
uint32_t dur = hal_input_get_press_duration(HAL_BTN_A); // 获取按压时长（毫秒）
hal_input_set_double_click_enabled(true);               // 启用/禁用双击检测
```

**默认按键映射**：

*📄 Source: [app_init.c](../../src/app/app_init.c#L469-L494)*

| 按键 | 短按 | 长按 |
|------|------|------|
| **BtnA** | 下一项 | 确认进入 |
| **BtnB** | 上一项 | 返回/取消 |

> **注意**：所有 item 类型的"确认"（switch 值翻转、slider 进入编辑、button 触发回调、list 进入子菜单、user 进入 App）统一由 **BtnA 长按**触发（`jump_to_selected_item()` → `xerintosh_dispatch_enter()`）。BtnA 短按仅做导航（`go_next_item()`）。

**⚠️ 陷阱**：
- `hal_input_get_event()` 是消费型调用：每个事件只返回一次，之后返回 `HAL_EVENT_NONE`
- 进入和退出 `user_item` 时**必须**调用 `hal_input_reset_events()`，否则残留按键会触发意外操作
- 框架在每帧开头统一调用 `hal_input_update()`，App 内不要重复调用

---

## 第三部分：内存管理速查表

*📄 Source: [ui_item_base.c](../../src/ui/ui_item_base.c#L130-L159) | [ui_item_list.c](../../src/ui/ui_item_list.c#L78-L104)*

| 资源 | 分配方 | 释放方 | 注意事项 |
|------|--------|--------|----------|
| item 结构体 | 创建函数（`malloc`） | `xerintosh_destroy_item_tree()` | 递归释放所有子项；仅对 user_item 调用 destroy_callback |
| `content` 字符串 | `xerintosh_init_base_item`（`strdup`） | `xerintosh_destroy_item_tree`（`free`） | 自动管理 |
| `switch/slider.value` 指针 | **调用方** | **调用方** | 框架只读写，不管生命周期 |
| `user_data` | 调用方 | 调用方（`destroy_callback`） | 框架不自动释放；`destroy_callback` **仅对 `user_item` 类型有效**，其他类型不调用 |
| `bitmap_data` | 调用方（常为 `static const`） | 调用方 | 框架只读 |

---

## 第四部分：回调中不能做的事

| 禁止/不推荐操作 | 原因 | 正确做法 |
|----------|------|----------|
| 在按钮回调中直接调用 `xerintosh_push_pop_up()` | 回调可能在 Xeros 内核调度上下文中执行，M5GFX FreeRTOS 信号量可能不可用 | 设标志位，`app_input_process()` 统一处理（一致性推荐） |
| 在回调中 `free()` item 自身 | item 可能在回调返回后被框架访问 | 用 `xerintosh_remove_item_from_list()` |
| 在中断上下文中调用 BT/WiFi API | `BluetoothSerial` 非线程安全 | 通过异步请求 API（如 `bt_mgr_request_enable()`） |
| 在编辑模式下读取 slider 绑定变量的值 | 编辑模式中变量处于中间"脏"状态，取消前值未确认 | 仅在 `exit_function` 回调中读取确认后的值 |

---

> **See Also:** [从零开始创建 App](your-first-app.md) | [开发者指南](../developer-guide.md) | [项目系统](../ui/item.md)
