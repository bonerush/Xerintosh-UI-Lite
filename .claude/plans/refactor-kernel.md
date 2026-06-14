# Xeros 内核层重构实施计划（阶段 2.1）

> **工作流**：`refactor-workflow` 阶段 2.1（内核层）  
> **工作区**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-ui`  
> **基线**：`pio run -e m5stick-c` ✅ / `pio test -e native` ✅（348 cases）  
> **日期**：2026-06-14

---

## 1. 目标

修复本轮内核层诊断中的全部 P0/P1 问题（K-P0-1 ~ K-P0-5、K-P1-1、K-P1-2、K-P1-9），不引入新功能，不触碰本轮排除项（K-P0-4 FD 表隔离、K-P1-3/K-P1-4 inode 引用计数）。

| ID | 问题 | 修复目标 |
|---|---|---|
| K-P0-1 | `kern_port_freertos.c:task_wrapper()` 任务自然返回未调用 `kern_exit()` | 统一走 `kern_exit()` 释放资源 |
| K-P0-2 | `kern_task_lifecycle.c:task_entry_trampoline()` 未调用 `kern_exit()` | 统一走 `kern_exit()` 释放资源 |
| K-P0-3 | `reap_zombies()` 未释放 `task->stack_base` | Native 路径回收栈内存 |
| K-P0-5 | `kern_dev_register()` / `kern_devfs_register_device()` / `kern_device_register()` 三轨并存 | 统一为 `kern_device_register()`，所有物理设备迁移到新模型 |
| K-P1-1 | 多处 API 返回 `int` 而非 `kern_err_t` | 引入 `kern_err_t` 并统一错误码返回类型 |
| K-P1-2 | `g_need_resched` 仅在 `CONFIG_PREEMPT_ENABLED` 时存在 | 无条件定义，消除非抢占构建编译错误 |
| K-P1-9 | mutex 无递归计数、unlock 不检查 owner | 增加递归计数与 owner 检查，返回 `kern_err_t` |

---

## 2. 风险与约束

- **禁止本轮修改**：VFS FD 表全局设计（K-P0-4）、inode/dentry 引用计数（K-P1-3/K-P1-4）。
- **每次只改一个职责**：一个任务只改一个函数/一个结构体/一个文件。
- **TDD**：每个代码改动前必须先补充或更新 native 测试。
- **Public header 变更必须同步 `doc/kernel/`**，附源链接与中文伪代码。
- **回滚**：每个任务独立可 `git revert`；任务之间串行推进。
- **ESP32 内存敏感**：不得引入新的每任务大内存开销。

---

## 3. 详细实施步骤（TDD）

### Task 1 — K-P0-2：Native 蹦床统一调用 `kern_exit()`

**目标**：修复 `task_entry_trampoline()` 资源泄漏，并建立可复用的回归测试。

**变更文件**：
- `src/kernel/kern_task_lifecycle.c`
- `test/test_native/test_kernel_task.cpp`

**测试先行**：

在 `test_kernel_task.cpp` 新增：

```cpp
static volatile bool g_release_called = false;

static void release_flag(void *arg)
{
    (void)arg;
    g_release_called = true;
}

static void no_exit_task(void *arg)
{
    (void)arg;
    kern_resource_track(g_current_task, (void*)0x1234,
                        KERN_RES_MEMORY, release_flag);
}

TEST(KernelTaskTest, NaturalReturnCallsExit)
{
    kern_sched_init();
    g_release_called = false;

    kern_pid_t pid = kern_spawn("no_exit", no_exit_task, NULL, 0);
    ASSERT_GE(pid, 0);

    for (int i = 0; i < 100 && !g_release_called; i++) {
        kern_sched_tick();
    }

    EXPECT_TRUE(g_release_called);
}
```

**代码改动**：

`src/kernel/kern_task_lifecycle.c:503-517` 改为：

```c
void task_entry_trampoline(void)
{
    kern_task_t *task = g_switch_to_task;

    if (task == NULL || task->entry == NULL) {
        setcontext(&g_sched_ctx);
        return;
    }

    task->entry(task->arg);

    /* 任务自然返回时，统一走 kern_exit() 释放资源 */
    kern_exit();
}
```

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelTaskTest.NaturalReturnCallsExit
pio test -e native
```

**回滚**：

```bash
git checkout -- src/kernel/kern_task_lifecycle.c test/test_native/test_kernel_task.cpp
```

---

### Task 2 — K-P0-1：FreeRTOS 任务包装器统一调用 `kern_exit()`

**目标**：修复 `task_wrapper()` 任务自然返回时的资源泄漏。

**变更文件**：
- `src/kernel/kern_port_freertos.c`

**代码改动**：

`src/kernel/kern_port_freertos.c:175-202` 改为：

```c
static void task_wrapper(void *arg)
{
    kern_task_t *task = (kern_task_t *)arg;
    uint8_t cpu = task->cpu_id;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;

    xSemaphoreTake(g_token_sem[cpu], portMAX_DELAY);

    task->state = KERN_TASK_RUNNING;

    if (task->entry != NULL) {
        task->entry(task->arg);
    }

    /* 任务自然返回时统一走 kern_exit()，避免资源泄漏 */
    kern_exit();

    /* kern_exit() 不会返回 */
}
```

**测试命令**：

```bash
pio run -e m5stick-c
```

**回滚**：

```bash
git checkout -- src/kernel/kern_port_freertos.c
```

---

### Task 3 — K-P0-3：`reap_zombies()` 释放 `task->stack_base`

**目标**：修复 Native / `XEROS_NATIVE_SCHED` 路径下每任务栈泄漏。

**变更文件**：
- `src/kernel/kern_task_lifecycle.c`
- `test/test_native/test_kernel_task.cpp`

**测试先行**：

新增高压力栈回收测试：

```cpp
TEST(KernelTaskTest, SpawnManyTasksDoesNotLeakStack)
{
    kern_sched_init();

    for (int round = 0; round < 50; round++) {
        kern_pid_t pid = kern_spawn("leak_test", simple_task, NULL, 0);
        ASSERT_GE(pid, 0);

        for (int i = 0; i < 100 && kern_task_get(pid) != NULL; i++) {
            kern_sched_tick();
        }

        EXPECT_EQ(kern_task_get(pid), nullptr);
    }
}
```

**代码改动**：

1. 在 `kern_task_kill()` 的 `XEROS_NATIVE_SCHED` 分支中，释放后显式置空指针：

```c
#if defined(XEROS_NATIVE_SCHED)
    if (task->stack_base != NULL) {
        free(task->stack_base);
        task->stack_base = NULL;
    }
```

2. 在 `reap_zombies()` 中 `free(zombie)` 之前新增：

```c
                if (zombie->stack_base != NULL) {
                    free(zombie->stack_base);
                    zombie->stack_base = NULL;
                }
```

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelTaskTest.SpawnManyTasksDoesNotLeakStack
pio test -e native
```

**回滚**：

```bash
git checkout -- src/kernel/kern_task_lifecycle.c test/test_native/test_kernel_task.cpp
```

---

### Task 4 — K-P0-5：迁移 `/dev/fb0` 到统一设备模型

**目标**：将 `dev_fb0` 从 `kern_file_ops_t` 改为 `kern_device_ops_t`。

**变更文件**：
- `src/kernel/devices/dev_fb0.c`
- `src/kernel/devices/dev_fb0.h`

**代码改动**：

重写为 `kern_device_ops_t`，逻辑不变，签名调整；`dev_fb0.h` 暴露 `extern kern_device_t g_fb0_dev;`。

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelDevicesTest.Fb0*
pio run -e m5stick-c
```

**回滚**：

```bash
git checkout -- src/kernel/devices/dev_fb0.c src/kernel/devices/dev_fb0.h
```

---

### Task 5 — K-P0-5：迁移 `/dev/input0` 到统一设备模型

**变更文件**：
- `src/kernel/devices/dev_input0.c`
- `src/kernel/devices/dev_input0.h`

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelDevicesTest.Input0*
pio run -e m5stick-c
```

---

### Task 6 — K-P0-5：迁移 `/dev/ttyS0` 到统一设备模型

**变更文件**：
- `src/kernel/devices/dev_ttyS0.cpp`
- `src/kernel/devices/dev_ttyS0.h`

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelDevicesTest.TtyS0*
pio run -e m5stick-c
```

---

### Task 7 — K-P0-5：统一 `kern_device_register()` 并创建 VFS 节点

**变更文件**：
- `src/kernel/kern_device.c`
- `src/kernel/kern_device.h`

**代码改动**：

`kern_device_register()` 同时完成全局链表注册 + `/dev/<name>` 节点创建。返回类型改为 `kern_err_t`。

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelDeviceTest.Register*
pio test -e native
```

---

### Task 8 — K-P0-5：移除旧设备注册 API 并更新 `kern_devices_init()`

**变更文件**：
- `src/kernel/kern_devfs.c`
- `src/kernel/kern_devfs.h`
- `src/kernel/devices/kern_devices.c`
- `test/test_native/test_kernel_devfs.cpp`

**代码改动**：

1. `kern_devfs.h` 删除 `kern_dev_register()` 与 `kern_devfs_register_device()` 声明。
2. `kern_devfs.c` 删除旧注册实现。
3. `kern_devices.c` 改为全部调用 `kern_device_register()`。

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelDevicesTest.AllDevicesRegistered
pio test -e native
pio run -e m5stick-c
```

---

### Task 9 — K-P1-1：引入 `kern_err_t` 类型

**变更文件**：
- `src/kernel/kern_types.h`

**代码改动**：

```c
/* 统一错误码返回类型 */
typedef int kern_err_t;
```

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelTypesTest.*
```

---

### Task 10 — K-P1-1：统一内核 API 返回类型为 `kern_err_t`

**变更文件**：
- `src/kernel/kern_resource.h/.c`
- `src/kernel/kern_vfs.h/.c`
- `src/kernel/kern_mpu.h/.c`
- `src/kernel/kern_task.h/.c`
- `src/kernel/kern_sysfs.h/.c`

**代码改动**：

将错误码类 API 的返回类型从 `int` 改为 `kern_err_t`，返回逻辑不变。

**测试命令**：

```bash
pio run -e m5stick-c
pio test -e native
```

---

### Task 11 — K-P1-2：无条件定义 `g_need_resched`

**变更文件**：
- `src/kernel/kern_smp.h`
- `test/test_native/test_kernel_smp.cpp`

**代码改动**：

将 `#define g_need_resched` 移出 `#ifdef CONFIG_PREEMPT_ENABLED`。

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelSmpTest.*
pio run -e m5stick-c
```

---

### Task 12 — K-P1-9：为 mutex 增加递归计数与 owner 检查

**变更文件**：
- `src/kernel/kern_sync.h`
- `src/kernel/kern_sync.c`
- `test/test_native/test_kernel_sync.cpp`

**代码改动**：

1. `mutex_t` 新增 `owner` 和 `recursive_count` 字段。
2. `mutex_init/lock/unlock` 改为返回 `kern_err_t`。
3. 增加递归计数和 owner 检查。

**测试命令**：

```bash
pio test -e native --gtest_filter=KernelSyncTest.*
pio test -e native
pio run -e m5stick-c
```

---

### Task 13 — 文档同步：类型系统与 SMP

**变更文件**：
- `doc/kernel/kern-types.md`
- `doc/kernel/kern-smp.md`
- `doc/kernel/kern-sync.md`

---

### Task 14 — 文档同步：设备模型与 devfs

**变更文件**：
- `doc/kernel/kern-device-model.md`
- `doc/kernel/kern-devfs.md`
- `doc/kernel/kern-devices.md`

---

## 4. 集成与回归验证

每完成一个 Task 后必须执行：

```bash
rm -rf .pio/build/
pio test -e native
pio run -e m5stick-c
```

全部 Task 完成后，执行全量回归：

```bash
rm -rf .pio/build/
pio test -e native
pio run -e m5stick-c
```

---

## 5. 验收标准

- [ ] `pio run -e m5stick-c` 退出码 0，无新增编译警告。
- [ ] `pio test -e native` 退出码 0，测试数 ≥ 348。
- [ ] K-P0-1 / K-P0-2：新增任务自然返回资源释放测试通过。
- [ ] K-P0-3：高压力 spawn/reap 测试通过，无 double-free。
- [ ] K-P0-5：所有设备注册走 `kern_device_register()`，旧 API 已移除。
- [ ] K-P1-1：所有错误码类 API 返回类型为 `kern_err_t`。
- [ ] K-P1-2：非抢占宏路径下 `g_need_resched` 可编译。
- [ ] K-P1-9：mutex 递归计数与 owner 检查测试通过。
- [ ] `doc/kernel/` 中所有受影响的 public header 文档已更新。
