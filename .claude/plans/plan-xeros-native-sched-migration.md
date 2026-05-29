# XEROS_NATIVE_SCHED 调度器迁移与内核加固计划

## 核心思路
将 WiFi/BT 管理器从 FreeRTOS-wrapped 任务迁移到 Xeros **原生**调度器（setjmp/longjmp），
彻底消除 FreeRTOS 双信号量协议导致的 kill/restart 死锁问题。

## 架构现状
```
当前: WiFi/BT → kern_spawn → kern_port.c → xTaskCreate → task_wrapper (FreeRTOS)
目标: WiFi/BT → kern_spawn → kern_task.c → setjmp/longjmp 原生上下文切换
```

## 实施步骤

### Step 1: 补全 kern_task.c 中 XEROS_NATIVE_SCHED 代码路径
- 文件: `src/kernel/kern_task.c`
- 为 `kern_sched_init/spawn/yield/exit/sleep_ms/sched_tick` 各添加 `#elif defined(XEROS_NATIVE_SCHED)` 分支
- 使用 `kern_ctx_esp32.h` 中的 `setjmp/longjmp` 替代 FreeRTOS 双信号量
- 声明 `static kern_ctx_t g_sched_ctx` 作为调度器上下文
- 验证: 带 `-DXEROS_NATIVE_SCHED` 编译通过

### Step 2: 启用 XEROS_NATIVE_SCHED
- 文件: `platformio.ini`
- 添加 `-D XEROS_NATIVE_SCHED` 编译标志
- 验证: 烧录后系统正常启动、菜单响应

### Step 3: WiFi 管理器 lazy-spawn
- 文件: `wifi_manager.cpp`
- 添加 `static kern_pid_t g_wifi_mgr_pid`
- `wifi_mgr_enable()`: 若任务ZOMBIE则 `kern_spawn` 重新创建
- `wifi_mgr_disable()`: 调用 `kern_task_kill(g_wifi_mgr_pid)` 终止任务
- 验证: 开关WiFi→任务列表中任务出现/消失

### Step 4: BT 管理器声明修复
- 文件: `bt_manager.cpp`
- 添加缺失的 `static kern_pid_t g_bt_mgr_pid` 声明
- 验证: 编译无警告

### Step 5: 显式 kern_exit() 调用
- 文件: `wifi_manager.cpp`, `bt_manager.cpp`
- `task_main()` 循环结束后显式 `kern_exit()`
- 验证: kill 后内核日志确认 exit 执行

### Step 6: ZOMBIE TCB 回收 (reap_zombies)
- 文件: `kern_task.c`
- 在 `kern_sched_tick()` 开始时扫描链表回收 ZOMBIE 非虚任务
- 解除链接→释放栈→free TCB→更新 count
- 验证: 反复 spawn/kill 不泄漏

### Step 7: 加固 kern_task_kill()
- 文件: `kern_task.c`
- XEROS_NATIVE_SCHED 路径: 标记 ZOMBIE + 释放栈 (reap_zombies 回收 TCB)
- FreeRTOS 路径: 增加 port_thread 有效性检查
- 验证: 各路径单元测试

### Step 8: 集成测试
- 基本启动 → 菜单响应
- WiFi/BT 各开关循环 5 次
- Task Manager kill → 验证 /proc/tasks
- 内存泄漏监控 (ESP.getFreeHeap)
