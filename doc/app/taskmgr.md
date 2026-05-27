# 任务管理器 App

> 源码: `src/app/taskmgr/taskmgr.c`, `src/app/taskmgr/taskmgr.h`

## 概述

任务管理器是一个 `user_item` 全屏 App，使用户可以在设备上直接查看和管理内核任务。

## 功能

1. **任务列表**: 显示所有运行中的内核任务（包括虚任务），格式为 `PID NAME STATE STACK`
2. **选择导航**: 短按 BtnA 下一项，短按 BtnB 上一项
3. **终止任务**: 对选中任务长按 BtnA，进入确认弹窗，再次长按 BtnA 确认终止
4. **退出**: 长按 BtnB 返回主菜单
5. **保护机制**: 系统关键任务（idle/shell/ui/taskmgr）不可终止

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
║  Task Manager        ║  ← 标题栏（绿色）
╠══════════════════════╣
║ 0  idle      RUN    ║  ← 受保护任务（绿色）
║ 1  shell     RUN    ║
║ 2  ui        RUN    ║
║ 3  wifi-mgr  SLEEP  ║  ← 普通任务（白色）
║ 4  bt-mgr    SLEEP  ║
║ 5  taskmgr   RUN    ║
╠══════════════════════╣
║ PID:3 wifi-mgr ...   ║  ← 底部信息栏（绿色）
╚══════════════════════╝
```

- **受保护任务**: 绿色字体，长按 A 无响应
- **选中行**: 反色高亮（白底黑字）
- **虚任务**: stack 字段显示 `n/a`

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
3. 匹配时拒绝操作并提示 "cannot kill system task"

## 虚任务支持

当用户进入 `user_item` App（如串口监视器）时，系统会自动为该 App 注册一个**虚任务**。虚任务：
- 无独立 FreeRTOS 上下文
- 不参与内核调度
- 在 `/proc/tasks` 和任务管理器中可见（stack 显示 `n/a`）
- 可通过 `kill` 或任务管理器终止（终止后 App 退出回主菜单）

## 注册到菜单

在 `app_init.c` 中：
```c
xerintosh_list_item_t* item2 = xerintosh_new_user_item(
    "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
```
