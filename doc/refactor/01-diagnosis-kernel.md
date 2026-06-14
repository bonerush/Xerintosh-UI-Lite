# Xeros 内核层重构前静态诊断报告

**目标项目**：`M5Stick-P1`（worktree `refactor-2026-06-14-kernel-ui`）  
**扫描范围**：`src/kernel/` 及 `src/kernel/devices/`  
**诊断维度**：API 一致性、错误码类型、任务生命周期、调度类、VFS、kmalloc、代码规模、空指针风险、模块规范、文档一致性  
**当前基线**：`pio run -e m5stick-c` / `pio test -e native` 均通过

---

## 问题总览

| 优先级 | 数量 | 说明 |
|--------|------|------|
| **P0** | 5 | 崩溃/资源泄漏/设计缺陷，必须修复 |
| **P1** | 11 | 接口不一致/悬空指针/竞态/功能缺陷，应修复 |
| **P2** | 9 | 代码规模/文档/死代码/风格，建议重构 |

---

## P0 严重问题

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|----|------|------|----------|--------------|----------|
| P0-1 | 任务生命周期 / Port 层 | `src/kernel/kern_port_freertos.c:175-202` | `task_wrapper()` 在任务入口返回后直接标记 ZOMBIE 并 `vTaskDelete(NULL)`，**未调用 `kern_exit()` 或 `kern_resource_release_all()`**。任务自然返回时，所有通过 `kern_kmalloc`/`kern_open` 追踪的 FD/内存/锁均泄漏。 | 在 `task_wrapper()` 返回路径统一调用 `kern_exit()`；或确保 `kern_resource_release_all()` 在 TCB 回收前执行。 | `test_kernel_task.cpp`、`test_kernel_resource.cpp` |
| P0-2 | 任务生命周期 / Native | `src/kernel/kern_task_lifecycle.c:503-517` | `task_entry_trampoline()` 同样不调用 `kern_exit()`，直接置 ZOMBIE 后切回调度器，资源泄漏。 | 在蹦床末尾调用 `kern_exit()`。 | `test_kernel_task.cpp` |
| P0-3 | 任务生命周期 / 内存 | `src/kernel/kern_task_lifecycle.c:459-497` | `reap_zombies()` 仅 `free(zombie)` 释放 TCB，**未释放 `stack_base`**；Native 与 `XEROS_NATIVE_SCHED` 路径下每任务栈泄漏。 | 在回收 TCB 前，对 Native/原生调度路径调用 `free(task->stack_base)`。 | `test_kernel_task.cpp`、`test_kernel_stack.cpp` |
| P0-4 | VFS / FD 表 | `src/kernel/kern_vfs.c:29-30`、`278-298` | 文件描述符表 `g_fd_table` 是**全局单表**，并非每任务隔离。多任务同时 `kern_open` 会共享 FD 槽位，且任务退出时的 FD 追踪语义错误。 | 将 FD 表移至 `kern_task_t` 内部，或实现每任务 FD 命名空间。 | `test_kernel_vfs.cpp` |
| P0-5 | DevFS / 设备模型 | `src/kernel/kern_devfs.c`、`kern_device.c`、`devices/kern_devices.c` | 设备注册存在**双轨 API**：旧版 `kern_dev_register()` 与新版 `kern_devfs_register_device()`/`kern_device_register()` 并存，实现重复、路径规则不一致。 | 统一为 `kern_device_t` 新模型，移除旧版 `kern_dev_register()` 及其实现。 | `test_kernel_devfs.cpp`、`test_kernel_device.cpp`、`test_kernel_devices.cpp` |

## P1 高优先级问题

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|----|------|------|----------|--------------|----------|
| P1-1 | 类型系统 / 错误码 | 多处 | 大量 API 返回 `int` 而非 `kern_err_t`。 | 统一将错误码返回类型声明为 `kern_err_t`。 | 全部 kernel 测试 |
| P1-2 | SMP / 抢占标志 | `src/kernel/kern_smp.h:80-82`、`kern_sched.c:246,273,300` | `g_need_resched` 宏仅在 `CONFIG_PREEMPT_ENABLED` 定义时存在，但 `kern_sched.c` 无条件使用。 | 无条件定义 `g_need_resched`，或把所有使用处也套上 `#ifdef CONFIG_PREEMPT_ENABLED`。 | `test_kernel_sched.cpp` |
| P1-3 | VFS / 引用计数 | `src/kernel/kern_vfs.c:184-215` | `kern_vfs_unlink()` 释放 dentry 时不检查是否有打开的文件。 | 为 inode/dentry 引入引用计数。 | `test_kernel_vfs.cpp` |
| P1-4 | VFS / inode 泄漏 | `src/kernel/kern_vfs.c:148-150` | `kern_dentry_register()` 替换已有 inode 时直接覆盖，不释放旧 inode。 | 在替换前释放旧 inode 或加入引用计数降级逻辑。 | `test_kernel_vfs.cpp` |
| P1-5 | VFS / 路径解析 | `src/kernel/kern_vfs.c:40-106` | `path_walk()` 不处理 `.` / `..`，也不支持相对路径。 | 实现 `.`/`..` 回退；严格校验路径长度。 | `test_kernel_vfs.cpp` |
| P1-6 | VFS / FD 泄漏 | `src/kernel/kern_vfs.c:347-352` | `kern_open()` 中 `kern_resource_track()` 失败时未关闭已分配的 FD。 | 在失败分支调用 `kern_close(fd)`。 | `test_kernel_vfs.cpp` |
| P1-7 | 内存分配器 | `src/kernel/kern_kmalloc.c:25-28`、`108-154` | `kmalloc_header_t` 未保证对齐；`kern_krealloc()` 在移动内存期间旧 ptr 已从资源链表移除。 | 手动对齐头；重构 `krealloc` 为“先分配-再迁移-最后释放旧块”。 | `test_kernel_kmalloc.cpp` |
| P1-8 | MPU / 栈守卫 | `src/kernel/kern_task_lifecycle.c:228`、`kern_mpu.c:134-140` | FreeRTOS 路径下 `task->mpu_config` 未分配。 | 在 spawn 时按需分配 `kern_mpu_config_t`。 | `test_kernel_mpu.cpp` |
| P1-9 | 同步原语 | `src/kernel/kern_sync.c:45-81` | `mutex_lock()` 允许递归加锁但无递归计数；`mutex_unlock()` 不检查持有者。 | 增加递归计数器；unlock 检查 owner。 | `test_kernel_sync.cpp` |
| P1-10 | 设备驱动 | `src/kernel/devices/dev_fb0.c:17-66` 等 | 设备 read/write 未校验 `buf` 空指针。 | 在入口增加 `if (buf == NULL) return KERN_EINVAL;`。 | `test_kernel_devices.cpp` |
| P1-11 | 头文件 | `src/kernel/kern_shell_cmds.h:21` | 重复 `typedef int16_t kern_fd_t;`。 | 移除该行，统一使用 `kern_types.h` 的定义。 | 编译全量检查 |

## P2 中低优先级问题

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|----|------|------|----------|--------------|----------|
| P2-1 | 代码规模 | `src/kernel/kern_shell_cmds.c`（971 行） | 远超 400 行限制。 | 按命令类别拆分。 | 间接 |
| P2-2 | sysfs / 路径一致性 | `src/kernel/kern_sysfs.c:194-198` | `log_level` 放在 `/sys/kernel/`，其余属性放在 `/sys/`。 | 统一所有 sysfs 属性到 `/sys/kernel/`。 | `test_kernel_sysfs.cpp` |
| P2-3 | 调度类接口 | `src/kernel/kern_sched_class.c:17-21` | `kern_sched_class_register()` 返回 `void`，失败时静默忽略。 | 改为返回 `kern_err_t`。 | `test_kernel_sched.cpp` |
| P2-4 | 调度类类型 | `src/kernel/kern_sched_class.h:68` | `pick_next_ready()` 声明为 `struct kern_task *`，与 `kern_task_t *` 不一致。 | 统一改为 `kern_task_t *`。 | `test_kernel_sched.cpp` |
| P2-5 | 死代码 | `src/kernel/kern_sched_rr.c:35` | `s_rr_last_prio` 声明后未使用。 | 删除。 | `test_kernel_sched.cpp` |
| P2-6 | 格式化库 | `src/kernel/kern_minprintf.c:83-243` | 不支持部分格式标志；`%zd` 使用错误。 | 补充格式解析；修正 `z` 修饰符。 | `test_kernel_init.cpp` |
| P2-7 | Shell 解析器 | `src/kernel/kern_shell_parser.c:28-48` | `\xNN` 解析可能越界。 | 先判断剩余长度再读取。 | 无 |
| P2-8 | 文档一致性 | `src/kernel/devices/kern_devices.h:5` | 注释未包含 `/dev/pwrkey`。 | 更新注释；同步文档。 | `test_kernel_devices.cpp` |
| P2-9 | 未初始化字段 | `src/kernel/kern_task_lifecycle.c:46,116,172` | `scheduler_class_id` 未显式初始化为 -1。 | 显式置 -1；调度类 enqueue/dequeue 时更新。 | `test_kernel_sched.cpp` |

## 重构优先级建议

1. **第一阶段（安全与稳定性）**：修复 P0-1、P0-2、P0-3 任务退出资源泄漏；修复 P0-4 VFS FD 表隔离问题。
2. **第二阶段（架构一致性）**：统一设备模型 P0-5；修正 P1 错误码类型、引用计数、MPU 配置。
3. **第三阶段（代码质量）**：拆分超大文件、修正 P2 文档与死代码、增强边界测试。
