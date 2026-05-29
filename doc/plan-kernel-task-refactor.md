# 内核任务管理重构计划

## Bug 1: 设置关闭WiFi/蓝牙后任务管理器仍显示在线

**根因**: `wifi_mgr_disable()` / `bt_mgr_disable()` 只关闭硬件和设置标志位，从未调用 `kern_exit()`。
FreeRTOS线程依然存活，通过信号量被Xeros调度器轮询调度，state始终在 READY→RUNNING→SLEEPING 循环，永不为 ZOMBIE。

## Bug 2: 杀死WiFi/蓝牙任务后功能未停止

**根因**: Task Manager 仅设置 `t->state = KERN_TASK_ZOMBIE`。
- Xeros调度器不再调度 → FreeRTOS线程永久阻塞在信号量上
- `wifi_mgr_disable()` 从未被调用 → ESP32 WiFi模块保持连接
- FreeRTOS线程未被 `vTaskDelete()` → 资源泄漏（~4KB栈+TCB）
- Shell的 `cmd_kill` 有同样问题

## 附加发现: ZOMBIE TCB内存泄漏
- `kern_exit()` 调用 `vTaskDelete()` 但TCB从未从链表移除/释放
- 虚任务有 `kern_task_unregister_virtual()` 正确清理，非虚任务无等价机制

## 实施步骤

### Step 1: 新增 `kern_port_thread_kill()`
- 文件: `kern_port.h` + `kern_port.c`
- 从外部销毁FreeRTOS线程: `vTaskDelete(thread)`
- Native环境添加空桩
- 验证: 编译通过

### Step 2: 新增 `kern_task_kill()` API  
- 文件: `kern_task.h` + `kern_task.c`
- 统一外部kill入口: 检查保护/自kill/虚任务 → 设ZOMBIE → 销毁线程
- 新增native测试: kill/protected/self/virtual
- 验证: `pio test -e native`

### Step 3: WiFi/BT管理器任务生命周期
- 文件: `wifi_manager.cpp` + `bt_manager.cpp`
- 添加 `g_wifi_task_should_exit` / `g_bt_task_should_exit` 标志
- `task_main()` 中检查标志 → `kern_exit()`
- `enable()`: 若任务已退出则重新 `kern_spawn()`
- `disable()`: 设置退出标志
- 验证: 硬件测试启用→禁用→任务列表中消失→再启用→重新出现

### Step 4: Task Manager kill路径修复
- 文件: `taskmgr_app.c`
- kill时按名称调用对应 `disable()` 清理硬件
- 使用新 `kern_task_kill()` API替换直接 `state=ZOMBIE`
- 验证: 连接WiFi → kill wifi-mgr → 路由器断开 → 任务消失

### Step 5: Shell kill命令修复
- 文件: `kern_shell_cmds.c`
- `cmd_kill`: 替换 `task->state = ZOMBIE` → `kern_task_kill(pid)`
- 验证: 串口shell执行 `kill <pid>`

### Step 6(可选): ZOMBIE TCB回收
- 在调度器中定期扫描回收ZOMBIE非虚任务
- 需同步修复Task Manager快照指针悬空问题
- 建议独立PR

## 实施顺序与依赖
| 步骤 | 依赖 | 验证 |
|------|------|------|
| Step 1: port_thread_kill | 无 | 编译 |
| Step 2: kern_task_kill | Step 1 | native test |
| Step 3: wifi/bt lifecycle | Step 2 | 硬件测试 Bug1 |
| Step 4: taskmgr kill | Step 2,3 | 硬件测试 Bug2 |
| Step 5: shell kill | Step 2 | 串口测试 |
