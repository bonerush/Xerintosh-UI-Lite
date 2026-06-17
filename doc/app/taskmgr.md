# 任务管理器 App

> **Parent:** [App 层索引](index.md) | **Related:** [串口监视器](serial-monitor.md), [行列表动画](../ui/ui-anim-row.md), [调度器](../kernel/kern-task.md)

## 概述

任务管理器是一个 `user_item` 全屏 App，使用户可以在设备上直接查看和管理内核任务。

## 功能

1. **任务列表**: 显示所有运行中的内核任务（包括虚任务），格式为 `PID NAME STATE STACK`
2. **选择导航**: 短按 BtnA 下一项，短按 BtnB 上一项
3. **终止任务**: 对选中任务长按 BtnA，进入确认弹窗，再次长按 BtnA 确认终止
4. **退出**: 长按 BtnB 返回主菜单
5. **保护机制**: 系统关键任务（idle/shell/ui/taskmgr）不可终止
6. **入场动画**: 行从屏幕底部滑入到最终位置（Phase 1 新增）
7. **选中切换动画**: 高亮框平滑过渡而非跳动（Phase 1 新增）

## 操作说明

| 操作 | 按键 | 说明 |
|------|------|------|
| 下一项 | BtnA 短按 | 选择器下移 |
| 上一项 | BtnB 短按 | 选择器上移 |
| 终止任务 | BtnA 长按 | 弹出确认对话框 |
| 确认终止 | BtnA 长按（确认态） | 执行终止操作 |
| 取消终止 | BtnB 长按（确认态）| 取消 |
| 退出 | BtnB 长按（正常态）| 返回主菜单 |

## 界面

```
╔══════════════════════╗
║  Task Manager        ║  ← 标题栏
╠══════════════════════╣
║ *0  idle      RUN    ║  ← 受保护任务（* 前缀标识，不可终止）
║ *1  shell     RUN    ║
║ *2  ui        RUN    ║
║  3  wifi-mgr  SLEEP  ║  ← 普通任务（可终止）
║  4  bt-mgr    SLEEP  ║
║ *5  taskmgr   RUN    ║
╠══════════════════════╣
║ PID:3 wifi-mgr ...   ║  ← 底部信息栏
╚══════════════════════╝
```

- **受保护任务**: 以 `*` 前缀标识，长按 A 无响应，会弹出 "Protected" 提示
- **选中行**: 反色高亮（白底黑字）
- **虚任务**: stack 字段显示 `n/a`
- **横屏 3 行布局**：header/footer 间距基于 `HAL_ROW_H()` 动态计算，80px 高度下可显示 3 行

---

## 动画系统（Phase 1）

### 架构

*📄 Source: [taskmgr.h](../../src/app/taskmgr/taskmgr.h#L17-L22), [taskmgr_app.c](../../src/app/taskmgr/taskmgr_app.c#L42-L52)*

任务管理器使用 `ui_anim_row` 公共动画工具：

```c
#include "ui/ui_anim_row.h"

// 状态结构体中嵌入动画上下文
typedef struct {
    int          selected;
    int          scroll;
    int          count;
    kern_task_t *tasks[KERN_MAX_TASKS];  /* 本帧任务指针快照 */
    bool         confirming;
    uint32_t     confirm_tick;
    xerintosh_anim_row_list_t anim_list;  // ← 动画上下文
    int          prev_selected;
    int          prev_scroll;
} taskmgr_state_t;
```

### 动画生命周期

*📄 Source: [taskmgr_app.c](../../src/app/taskmgr/taskmgr_app.c#L101-L119)*

```c
taskmgr_init()
  └─ xerintosh_anim_row_list_init(&anim_list, visible, TASKMGR_ROW_H, list_top)
       → 所有行 Y = SCREEN_HEIGHT（底部起点）
       → 高亮框 Y = SCREEN_HEIGHT
```

*📄 Source: [taskmgr_app.c](../../src/app/taskmgr/taskmgr_app.c#L165-L258)*

```c
taskmgr_loop() — 每帧
  ├─ 输入处理（更新 selected/scroll）
  ├─ 如果 selected 或 scroll 变化:
  │    xerintosh_anim_row_list_refresh(&anim_list, ...)
  │    → 重新计算所有 trg 值
  └─ xerintosh_anim_row_list_update(&anim_list, (float)ANIM_SPEED_SELECTOR)
       → 驱动当前值向目标值缓动

taskmgr_draw() → draw_list()
  ├─ rows[i].y   → 每行的绘制 Y 坐标
  └─ highlight.y/w → 高亮框位置和宽度
```

### 动画效果

- **入场动画**：所有行从屏幕底部（`SCREEN_HEIGHT`）滑入到最终行位置，约 300ms
- **选中切换**：高亮框从旧行位置平滑滑到新行位置，约 150ms
- **滚动平滑**：`scroll_offset` 浮点插值消除整数滚动的跳动感

### 横屏布局

*📄 Source: [taskmgr.h](../../src/app/taskmgr/taskmgr.h#L17-L22)*

```c
#define TASKMGR_LEFT_MARGIN  HAL_LEFT_X()         /* 标准左缩进 = 4 */
#define TASKMGR_HEADER_H     HAL_ROW_H()           /* 标题栏高度 = font_h + 4 */
#define TASKMGR_FOOTER_H     HAL_ROW_H()           /* 底部信息栏高度 = font_h + 4 */
#define TASKMGR_ROW_H        HAL_ROW_H()           /* 列表行高 = font_h + 4 */
```

*📄 Source: [taskmgr_app.c](../../src/app/taskmgr/taskmgr_app.c#L127-L137)*

```c
int taskmgr_visible_lines(void)
{
    int16_t header_h = TASKMGR_HEADER_H;
    int16_t footer_h = TASKMGR_FOOTER_H;
    int16_t avail = SCREEN_HEIGHT - header_h - footer_h;
    int16_t row_h = TASKMGR_ROW_H;
    int visible = avail / row_h;
    if (visible > TASKMGR_VISIBLE_MAX) visible = TASKMGR_VISIBLE_MAX;
    if (visible < 1) visible = 1;
    return visible;
}
```

竖屏 160px 下不受影响，仍可显示 ≥7 行。横屏 80px 下 `HAL_ROW_H() ≈ 16`，可用 `80 - 16 - 16 = 48`，`48 / 16 = 3` 行。

---

## 保护机制

以下任务受保护，**无法通过任务管理器或 Shell `kill` 命令终止**：

| 任务名 | PID | 说明 |
|--------|-----|------|
| idle | 0 | 调度器空闲任务，Xeros 核心 |
| shell | 1 | 交互式命令行，失去则无法控制 |
| ui | 2 | UI 框架，失去则屏幕冻结 |
| taskmgr | 动态 | 任务管理器自身 |

保护逻辑：
1. `kern_task_is_protected()` 检查任务名是否在保护列表中
2. Shell `kill` 和任务管理器均调用此检查
3. 匹配时拒绝操作并提示 "Protected"

*📄 Source: [taskmgr_app.c](../../src/app/taskmgr/taskmgr_app.c#L172-L198)*

```c
if (event_a == HAL_EVENT_LONG_PRESS) {
    if (g_tm.selected >= 0 && g_tm.selected < g_tm.count) {
        kern_task_t *t = g_tm.tasks[g_tm.selected];
        if (t != NULL && !kern_task_is_protected(t)) {
            g_tm.confirming = true;
            g_tm.confirm_tick = hal_get_ticks();
        } else {
            xerintosh_push_pop_up("Protected", 1500);
        }
    }
}
```

## 虚任务支持

当用户进入 `user_item` App（如串口监视器）时，系统会自动为该 App 注册一个**虚任务**。虚任务：
- 无独立 FreeRTOS 上下文
- 不参与内核调度
- 在 `/proc/tasks` 和任务管理器中可见（stack 显示 `n/a`）
- 可通过 `kill` 或任务管理器终止（终止后 App 退出回主菜单）

## 注册到菜单

*📄 Source: [app_init.c](../../src/app/app_init.c#L152-L153)*

```c
xerintosh_list_item_t* item2 = xerintosh_new_user_item(
    "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
```

---

> **See Also:** [串口监视器](serial-monitor.md) | [行列表动画](../ui/ui-anim-row.md) | [调度器](../kernel/kern-task.md) | [内核 Shell](../kernel/kern-shell.md)
