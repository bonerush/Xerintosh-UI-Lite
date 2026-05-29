# UI 任务（UI Task）

> **Parent:** [App 层索引](index.md) | **Related:** [核心引擎](../ui/core.md), [输入系统](../hal/input.md), [协作式调度器](../kernel/kern-task.md)

## 概述

`ui_task` 是将 Xerintosh UI 主循环包装为 **Xeros 内核任务**的入口。它将原本直接在 Arduino `loop()` 中运行的 UI 渲染逻辑，迁移到内核调度器的协作式多任务框架中，使 UI 与其他任务（如 WiFi、蓝牙、Shell）可以共享 CPU。

---

## 任务主循环

*📄 Source: [ui_task.c](../../src/app/ui_task.c#L36-L77)*

```c
void ui_task_main(void *arg)
{
    (void)arg;
    static int frame = 0;

    kern_log(KERN_LOG_INFO, "ui_task_main started");

    for (;;) {
        frame++;
        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d begin", frame);
        }

        app_input_process();

        hal_display_clear();
        xerintosh_ui_main_core();
        xerintosh_ui_widget_core();

        /* 长按提示动画 */
        uint32_t dur_a = hal_input_get_press_duration(HAL_BTN_A);
        uint32_t dur_b = hal_input_get_press_duration(HAL_BTN_B);
        if (dur_a > 0 && dur_a < 500) {
            xerintosh_draw_long_press_hint(dur_a, 500);
        } else if (dur_b > 0 && dur_b < 500) {
            xerintosh_draw_long_press_hint(dur_b, 500);
        }

        hal_display_flush();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d flush done, yielding", frame);
        }

        /* 协作式让出 CPU */
        kern_yield();

        if (frame <= 5) {
            kern_log(KERN_LOG_INFO, "ui frame %d resumed after yield", frame);
        }
    }
}
```

### 中文伪代码拆解

```
函数 UI任务入口(参数) {
    静态帧计数器 = 0
    记录日志: "UI任务已启动"

    无限循环 {
        帧计数器++
        if (前5帧) 记录日志: "第N帧开始"

        // 第一步：处理输入
        应用输入处理()    // 读取按键，映射到UI导航

        // 第二步：清除后台缓冲区
        显示清屏()

        // 第三步：UI核心渲染
        UI主循环核心()    // 列表渲染 或 user_item App 渲染
        UI控件核心()      // 信息栏、弹窗动画与绘制

        // 第四步：长按提示
        读取按键A长按持续时间
        读取按键B长按持续时间
        if (任一按键正在长按且未满500ms) {
            绘制长按进度条(当前时长, 阈值500ms)
        }

        // 第五步：刷新到屏幕
        显示刷新()

        if (前5帧) 记录日志: "第N帧刷新完成，准备让出"

        // 第六步：协作式让出CPU
        内核让出()        // 切换到下一个内核任务

        if (前5帧) 记录日志: "第N帧恢复执行"
    }
}
```

**核心思想**：UI 任务是一个永不退出的无限循环任务。每帧执行“输入→清屏→渲染→刷新→yield”五步，通过 `kern_yield()` 主动让出 CPU，使 WiFi/BT/Shell 等其他任务获得执行机会。

---

## 每帧渲染顺序

```
┌─────────────────────────────────────────┐
│ 1. app_input_process()                  │  ← 读取按键事件
│ 2. hal_display_clear()                  │  ← 清除 M5Canvas 后台缓冲区
│ 3. xerintosh_ui_main_core()             │  ← 列表/user_item 渲染
│ 4. xerintosh_ui_widget_core()           │  ← 信息栏/弹窗渲染
│ 5. xerintosh_draw_long_press_hint()     │  ← 可选：长按进度条
│ 6. hal_display_flush()                  │  ← pushSprite DMA 刷新
│ 7. kern_yield()                         │  ← 让出 CPU
└─────────────────────────────────────────┘
```

---

## 与旧架构的区别

| 方面 | 旧架构（直接在 loop()） | 新架构（ui_task） |
|------|------------------------|------------------|
| 运行位置 | Arduino `loop()` | Xeros 内核任务 |
| 调度方式 | 独占 CPU | 协作式 yield |
| 与其他任务关系 | WiFi/BT 在 loop() 中轮询 | WiFi/BT 作为独立内核任务 |
| 启动方式 | `setup()` 后直接调用 | `kern_spawn("ui", ui_task_main, ...)` |

---

## 启动流程

*📄 Source: [main.cpp](../../src/main.cpp#L150-L170)*

```c
/* main.cpp setup() 中 */
kern_spawn("ui", ui_task_main, NULL, 4096);   /* 4KB 栈 */
```

UI 任务在 `main.cpp` 的 `setup()` 末尾通过 `kern_spawn()` 创建，栈大小 4096 字节。

---

## 注意事项

1. **WiFi/BT 不再在 UI 任务中轮询**：`wifi_mgr_task_main` 和 `bt_mgr_task_main` 是独立的内核任务，由各自的调度循环驱动。UI 任务中不再调用 `wifi_mgr_update()` 或 `bt_mgr_update()`。

2. **HAL 调用保持直接**：当前阶段 UI 任务仍直接调用 `hal_display_clear()` 和 `hal_display_flush()`，不经过 VFS 的 `/dev/fb0`。这是为了性能考虑，后续可能逐步迁移到 VFS 写入协议。

3. **前5帧日志**：启动初期会打印详细的帧生命周期日志，帮助调试调度问题。5 帧后停止，避免日志洪水。

---

> **See Also:** [App 层索引](index.md) | [核心引擎](../ui/core.md) | [协作式调度器](../kernel/kern-task.md)
