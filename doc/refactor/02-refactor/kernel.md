# 阶段 2.1 内核层重构报告

## 目标

修复诊断阶段识别的 P0/P1 问题，统一设备注册入口、错误码类型、任务退出路径与同步原语语义。

## 已完成项

| ID | 问题 | 关键修改文件 |
|----|------|--------------|
| K-P0-1 | FreeRTOS 任务自然返回未释放资源 | `src/kernel/kern_port_freertos.c` |
| K-P0-2 | Native 任务蹦床未调用 `kern_exit()` | `src/kernel/kern_task_lifecycle.c` |
| K-P0-3 | `reap_zombies()` 未释放 Native 栈 | `src/kernel/kern_task_lifecycle.c` |
| K-P0-5 | 设备注册双轨 API | `src/kernel/kern_device.c`, `src/kernel/kern_devfs.c/h`, `src/kernel/devices/kern_devices.c`, `src/kernel/devices/dev_fb0.c/h`, `src/kernel/devices/dev_input0.c/h`, `src/kernel/devices/dev_ttyS0.cpp/h` |
| K-P1-1 | 错误码 API 返回类型不统一 | `src/kernel/kern_types.h`, `src/kernel/kern_resource.h/.c`, `src/kernel/kern_vfs.h/.c`, `src/kernel/kern_mpu.h/.c`, `src/kernel/kern_task.h`, `src/kernel/kern_task_lifecycle.c`, `src/kernel/kern_sysfs.h/.c`, `src/kernel/kern_device.h/.c`, `src/kernel/devices/kern_devices.h/.c` |
| K-P1-2 | `g_need_resched` 编译依赖 `CONFIG_PREEMPT_ENABLED` | `src/kernel/kern_smp.h`, `test/test_native/test_kernel_smp.cpp` |
| K-P1-9 | mutex 无递归计数 / unlock 不检查 owner | `src/kernel/kern_types.h`, `src/kernel/kern_sync.h/.c`, `test/test_native/test_kernel_sync.cpp` |

## 验证结果

- `pio run -e m5stick-c`：✅ SUCCESS
- `pio test -e native`：✅ 354 test cases passed

## 重要设计变更

### 1. `kern_device_register()` 成为唯一设备注册入口

现在 `kern_device_register(dev)` 同时完成：
1. 将设备加入全局链表（`g_device_list`）；
2. 自动创建 `/dev/<name>` VFS 节点；
3. 使用 bridge fops 将 `kern_device_ops_t` 桥接到 `kern_file_ops_t`。

旧版 `kern_dev_register()` 和 `kern_devfs_register_device()` 已从源码中移除。

### 2. 错误码类型统一

- 新增 `kern_err_t` 类型别名（`typedef int kern_err_t;`）；
- 新增 `KERN_EPERM`（-40）用于 mutex owner 检查失败；
- 将资源/VFS/MPU/任务/sysfs/设备注册等 API 返回类型统一为 `kern_err_t`。

### 3. 任务退出路径统一

- Native 路径：`task_entry_trampoline()` 末尾调用 `kern_exit()`；
- FreeRTOS 路径：`task_wrapper()` 末尾调用 `kern_exit()`；
- `reap_zombies()` 在释放 TCB 前释放 `stack_base`（Native 路径）。

### 4. mutex 语义强化

- 新增 `recursive_count` 字段；
- `mutex_lock()` 对同 owner 递归时递增计数；
- `mutex_unlock()` 检查 owner，非 owner 返回 `KERN_EPERM`；
- 计数归零时才真正释放 owner。

## 未处理项（本轮排除）

| ID | 问题 | 原因 |
|----|------|------|
| K-P0-4 | FD 表全局单表，非每任务隔离 | 会改动 `kern_task_t` 结构，影响 VFS/Shell/资源子系统，风险过高，留待专门迭代 |
| K-P1-3 | `kern_vfs_unlink()` 不检查打开引用 | 依赖 K-P0-4 的 FD 表重构 |
| K-P1-4 | 替换 inode 时泄漏旧 inode | 依赖引用计数体系 |
| K-P1-5 | VFS 路径不支持 `.` / `..` | 功能增强，非本轮重点 |
| K-P1-6 | `kern_open()` 资源追踪失败时 FD 泄漏 | 可单独修复，本轮未处理 |
| K-P1-7 | kmalloc 对齐与 krealloc 安全 | 需要更细致内存子系统重构 |
| K-P1-8 | MPU 配置未分配 | ESP32 路径当前未触发 |

## 新增/更新测试

- `test_kernel_task.cpp`: `NaturalReturnCallsExit`, `SpawnManyTasksDoesNotLeakStack`
- `test_kernel_smp.cpp`: `NeedReschedAvailableWithoutPreempt`
- `test_kernel_sync.cpp`: `MutexRecursiveLockIncrementsCount`, `MutexUnlockByNonOwnerFails`, 更新 `MutexUnlockWithoutLockReturnsPermError`
- `test_kernel_device.cpp`: `RegisterCreatesDevNode`
- `test_kernel_devfs.cpp`: 重写为使用 `kern_device_register()`
- `test_kernel_devices.cpp`: 全部设备走新模型

## 下一步

进入阶段 2.3：UI 核心层重构。
