# 内核层重构报告

## 范围

- 处理诊断问题：K-P0-01、K-P0-02、K-P0-03、K-P0-04、K-P1-01、K-P1-03、K-P1-07、K-P1-08、K-P2-03
- 延后问题：K-P1-02、K-P1-04、K-P1-05、K-P1-06、K-P2-01、K-P2-02、K-P2-04

## 变更摘要

| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改文件 | 20+ | 内核源码、设备实现、native 测试 |
| 新增 API | 4 | `kern_kmalloc_untracked`、`kern_kfree_untracked`、`kern_kmalloc_for_task`、`kern_vfs_inode_ref_count`（测试用） |
| 行为修复 | 4 | FD 隔离、资源节点回收、栈归属、inode 引用计数 |
| 接口统一 | 1 | 调度类/设备 ops 返回 `kern_err_t` |
| 新增测试 | 20+ | 覆盖 FD 隔离、引用计数、kmalloc untracked、栈追踪、调度类 ID、资源锁 |

## 详细变更

### 1. T8：删除 `kern_sched_rr.c` 未使用的 `s_rr_last_prio`
**原因**：K-P2-03  
**实现**：删除 `static uint8_t s_rr_last_prio = 0;` 及其注释。  
**影响接口**：无 public API 变化。  
**文档更新**：无需更新。

### 2. T5：统一内核 API 返回类型为 `kern_err_t`
**原因**：K-P1-01  
**实现**：
- `kern_sched_class_register()` 由 `void` 改为 `kern_err_t`。
- `procfs_register_file()` 由 `int` 改为 `kern_err_t`。
- `kern_device_ops_t` 中 `open/close/read/write/ioctl` 统一返回 `kern_err_t`。
- 更新所有设备实现（`dev_fb0.c`、`dev_input0.c`、`dev_pwrkey.c`、`dev_ttyS0.cpp`）及测试桩。
**影响接口**：
- `kern_sched_class_register()` 返回值变为 `kern_err_t`。
- 设备 ops 回调签名变化。  
**文档更新**：`doc/kernel/kern-device.md`、`doc/kernel/kern-sched.md`（阶段 2.5 文档同步时更新）。

### 3. T7：TCB `scheduler_class_id` 初始化与同步
**原因**：K-P1-08  
**实现**：
- `kern_sched_class_t` 新增 `int8_t class_id`。
- `kern_sched_class_register()` 注册时赋值 `class_id`。
- RR/FIFO 的 `enqueue` 设置 `task->scheduler_class_id`，`dequeue` 清 `-1`。
- 所有 `kern_spawn` 路径初始化 `scheduler_class_id` 为 `KERN_SCHED_CLASS_RR_ID`。
**影响接口**：新增宏 `KERN_SCHED_CLASS_RR_ID`。  
**文档更新**：`doc/kernel/kern-sched.md`（阶段 2.5 更新）。

### 4. T2：资源追踪节点使用非递归分配器
**原因**：K-P0-02  
**实现**：
- 重构 `kern_kmalloc` 内部为 `kern_kmalloc_impl(size, owner, track)`。
- 新增 `kern_kmalloc_untracked()` / `kern_kfree_untracked()`。
- `kern_resource.c` 中资源节点本身改用 untracked 分配，避免递归。
**影响接口**：新增 `kern_kmalloc_untracked`、`kern_kfree_untracked`。  
**文档更新**：`doc/kernel/kern-kmalloc.md`（阶段 2.5 更新）。

### 5. T6：资源追踪链表加锁
**原因**：K-P1-07  
**实现**：
- `kern_task_t` 新增 `volatile bool resource_lock`。
- `kern_resource.c` 中 `track`/`untrack`/`release_all` 对 `resource_head` 加锁。
- SMP 路径使用 `__sync_lock_test_and_set` / `__sync_lock_release`，单核为空操作。
**影响接口**：无 public API 变化（锁字段为内部使用）。  
**文档更新**：无需更新。

### 6. T3：Native 任务栈归属任务自身
**原因**：K-P0-03  
**实现**：
- 新增 `kern_kmalloc_for_task(task, size)`，将分配记录到指定任务资源链表。
- Native / `XEROS_NATIVE_SCHED` 路径下 `task_stack_init()` 改用 `kern_kmalloc_for_task()`。
- 移除 `kern_task_kill()` / `reap_zombies()` 中手动 `free(stack_base)`。
- `kern_exit()` 释放资源后置 `stack_base = NULL`。
**影响接口**：新增 `kern_kmalloc_for_task`。  
**文档更新**：`doc/kernel/kern-kmalloc.md`、`doc/kernel/kern-task.md`（阶段 2.5 更新）。

### 7. T4：inode/dentry 引用计数
**原因**：K-P0-04  
**实现**：
- `kern_inode_t` 新增 `uint32_t ref_count`。
- 新增内部 `kern_inode_ref` / `kern_inode_unref` / `kfree_inode`。
- `kern_dentry_register`、`kern_open`、`kern_close`、`kern_vfs_unlink` 维护引用计数。
- `NATIVE_TEST` 下暴露 `kern_vfs_inode_ref_count()` 供测试使用。
**影响接口**：无 public API 变化（测试 helper 仅在 `NATIVE_TEST` 可用）。  
**文档更新**：`doc/kernel/kern-vfs.md`（阶段 2.5 更新）。

### 8. T1：每任务 FD 命名空间与 `kern_open` 失败回滚
**原因**：K-P0-01、K-P1-03  
**实现**：
- `kern_task_t` 新增 `kern_file_t *fd_table[KERN_MAX_FD_PER_TASK]`。
- 删除全局 `g_fd_table`。
- `fd_alloc` / `fd_get` / `kern_open` / `kern_close` 全部基于当前任务的 `fd_table`。
- `kern_open` 失败路径回收 FD 槽位。
- 所有 `kern_spawn` 路径初始化 `fd_table` 为全 NULL。
- FD 资源追踪使用 `fd + 1` 作为 cookie，避免 `fd == 0` 时 ptr 为 NULL。
**影响接口**：无 public API 变化，但 FD 不再全局共享。  
**文档更新**：`doc/kernel/kern-vfs.md`、`doc/kernel/kern-task.md`（阶段 2.5 更新）。

## 测试

- 新增测试：
  - `test/test_native/test_kernel_vfs.cpp`：FD 隔离、open 失败回滚、inode 引用计数
  - `test/test_native/test_kernel_kmalloc.cpp`：untracked 分配器
  - `test/test_native/test_kernel_stack.cpp`：栈归属任务
  - `test/test_native/test_kernel_resource.cpp`：资源锁
  - `test/test_native/test_kernel_sched.cpp`：调度类 ID、返回类型
  - `test/test_native/test_kernel_device.cpp`：设备 ops 返回类型
- 验证结果：
  - `pio test -e native`：✅ PASS（394 个测试用例）
  - `pio run -e m5stick-c`：✅ PASS（Flash 88.0%，RAM 22.3%）

## 检查清单

- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位（无新增继承）
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`（无新增回调）
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [ ] 文档已同步更新（阶段 2.5 统一处理）
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点

- 每个子任务均为独立 commit，可单独 `git revert <commit>`。
- 统一回滚到阶段 2.1 开始前：`git reset --hard a4d8bab2703908ff0c4934daf97c1bced671eb40`。

## 遗留问题

| ID | 问题 | 后续处理 |
|----|------|----------|
| K-P1-02 | `path_walk()` 不支持 `.` / `..` | 延后到后续内核重构轮次 |
| K-P1-04 | `kmalloc_header_t` 未显式对齐 | 延后 |
| K-P1-05 | `kern_krealloc()` 非原子语义 | 延后 |
| K-P1-06 | FIFO 调度抢占语义 | 延后 |
| K-P2-01 | `kern_shell_cmds.c` 文件过长 | 延后到文档/风格轮次 |
| K-P2-02 | sysfs 路径不一致 | 延后 |
| K-P2-04 | `minprintf` `%zd` 支持 | 延后 |
