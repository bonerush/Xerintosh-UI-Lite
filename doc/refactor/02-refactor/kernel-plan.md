# 阶段 2.1 内核层重构微观实施计划

## 1. 本轮内核层目标

### 处理的问题（P0 全处理 + 高价值 P1）
- **P0（4 项）**：K-P0-01、K-P0-02、K-P0-03、K-P0-04
- **P1（4 项）**：K-P1-01、K-P1-03（与 K-P0-01 同文件，合并）、K-P1-07、K-P1-08
- **P2（1 项）**：K-P2-03（死代码删除，零风险）

### 明确延后的问题
- **P1 延后**：K-P1-02（`path_walk` `.`/`..`）、K-P1-04/K-P1-05（kmalloc 对齐与 krealloc 原子语义）、K-P1-06（FIFO 抢占语义）
- **P2 延后**：K-P2-01（Shell 命令拆分）、K-P2-02（sysfs 路径统一）、K-P2-04（minprintf `%zd`）

> 延后理由：K-P1-02/04/05/06 属于功能增强或内存子系统深度改造，超出本轮“6~8 个可独立验证子任务”的控制范围；K-P2-01/02/04 为文档/风格问题，放到阶段 2.5 文档体系同步更划算。

---

## 2. 子任务列表

### T1：K-P0-01 + K-P1-03 — 每任务 FD 命名空间与 `kern_open` 失败回滚
- **目标问题**：`g_fd_table` 全局单表；`kern_open` 失败时 FD 槽位泄漏
- **变更文件**：
  - `src/kernel/kern_task.h`
  - `src/kernel/kern_vfs.c`
  - `src/kernel/kern_vfs.h`（如需要新增 helper）
  - `test/test_native/test_kernel_vfs.cpp`
- **步骤**：
  1. 精读 `kern_vfs.c` 中 `fd_alloc`/`fd_get`/`kern_open`/`kern_close`。
  2. 在 `kern_task_t` 中新增 `kern_file_t fd_table[KERN_MAX_FD_PER_TASK]`。
  3. **先写失败测试**：
     - `FdNamespaceIsolated`：父任务占满全部 FD 后，`kern_spawn` 的子任务仍能成功 `kern_open`（修复前返回 `KERN_EMFILE`）。
     - `OpenFailureReleasesFd`：构造一个 `fops->open` 失败的 inode，`kern_open` 失败后立即再次 `kern_open` 应拿到同一槽位（验证槽位已释放）。
  4. 实现：
     - `fd_alloc`/`fd_get` 改为操作 `kern_task_current()->fd_table`。
     - 删除全局 `g_fd_table`，`kern_vfs_init` 中删除对应 `memset`。
     - `kern_open` 在 `fd_alloc` 后任意失败路径调用 `kern_close(fd)` 并清空当前任务 FD 表槽位。
  5. 运行 `pio test -e native` + `pio run -e m5stick-c`。
  6. `git commit`。
- **回滚策略**：`git revert` 本次 commit，恢复全局 `g_fd_table`。
- **验收标准**：
  - 新增 2 个测试通过
  - 全部 VFS/resource/task 测试通过
  - `m5stick-c` 编译成功

---

### T2：K-P0-02 — 资源追踪节点使用内核分配器（非递归路径）
- **目标问题**：`kern_resource.c` 直接用 `malloc`/`free` 分配资源节点
- **变更文件**：
  - `src/kernel/kern_kmalloc.c`
  - `src/kernel/kern_kmalloc.h`
  - `src/kernel/kern_resource.c`
  - `test/test_native/test_kernel_kmalloc.cpp`
- **步骤**：
  1. 精读 `kern_kmalloc.c` 头部实现与 `kern_resource_track` 的调用关系，确认不能直接用 `kern_kmalloc` 分配节点（会递归）。
  2. 重构 `kern_kmalloc` 为内部 `kern_kmalloc_impl(size, owner, track)`：
     - `kern_kmalloc(size)` → `impl(size, current, true)`
     - 新增 `kern_kmalloc_untracked(size)` → `impl(size, NULL, false)`
     - 新增 `kern_kfree_untracked(ptr)` → 直接 `free(hdr)`
  3. **先写失败测试**：
     - `UntrackedAllocNotTracked`：`kern_kmalloc_untracked` 返回非 NULL，且分配后当前任务 `resource_head` 不增长。
     - `UntrackedFreeDoesNotCrash`：`kern_kfree_untracked(NULL)` 与正常释放均不崩溃。
  4. 将 `kern_resource.c` 中的 `malloc`/`free` 替换为 `kern_kmalloc_untracked`/`kern_kfree_untracked`。
  5. 验证。
  6. `git commit`。
- **回滚策略**：恢复 `kern_resource.c` 的 `malloc`/`free`；移除 `kern_kmalloc_untracked` API。
- **验收标准**：
  - 新增 2 个测试通过
  - 资源测试全部通过
  - `m5stick-c` 编译成功

---

### T3：K-P0-03 — Native 任务栈使用 `kern_kmalloc` 并归属任务自身
- **目标问题**：`task_stack_init` 用 `malloc`，栈不纳入资源追踪
- **依赖**：T2（需要 `kern_kmalloc_impl`）
- **变更文件**：
  - `src/kernel/kern_kmalloc.c` / `.h`（新增 `kern_kmalloc_for_task`）
  - `src/kernel/kern_task_stack.c`
  - `src/kernel/kern_task_lifecycle.c`
  - `test/test_native/test_kernel_stack.cpp`
- **步骤**：
  1. 在 `kern_kmalloc` 中新增 `kern_kmalloc_for_task(task, size)` → `impl(size, task, true)`。
  2. **先写失败测试**：
     - `StackTrackedToOwnerTask`：`kern_spawn` 后检查子任务 `resource_head` 中必须存在一条 `KERN_RES_MEMORY` 记录，其 `ptr == task->stack_base`。
     - `StackFreedOnTaskExit`：子任务自然退出并被 `reap_zombies` 回收后，父任务再次 `kern_spawn` 50 轮不崩溃、不泄漏。
  3. 修改 `task_stack_init`（`NATIVE_TEST` / `XEROS_NATIVE_SCHED` 路径）：
     - `task->stack_base = (uint8_t *)kern_kmalloc_for_task(task, stack_size);`
  4. 修改 `kern_task_lifecycle.c`：
     - 删除 `kern_task_kill` 与 `reap_zombies` 中 `free(task->stack_base)` 的手动释放。
     - 在 `kern_exit` 中 `kern_resource_release_all(cur)` 后显式置 `cur->stack_base = NULL`（防止后续代码误用已释放栈）。
  5. 验证。
  6. `git commit`。
- **回滚策略**：恢复 `task_stack_init` 的 `malloc`；恢复手动 `free`。
- **验收标准**：
  - 新增 2 个测试通过
  - `SpawnManyTasksDoesNotLeakStack` 仍通过
  - `m5stick-c` 编译成功

---

### T4：K-P0-04 — inode/dentry 引用计数与 `kern_vfs_unlink` 修复
- **目标问题**：`kern_vfs_unlink` 释放 dentry 时未释放 inode，也不检查打开引用
- **变更文件**：
  - `src/kernel/kern_vfs.c`
  - `src/kernel/kern_vfs.h`
  - `test/test_native/test_kernel_vfs.cpp`
- **步骤**：
  1. 在 `kern_inode_t` 中新增 `uint32_t ref_count`。
  2. 新增内部 `kern_inode_ref(inode)` / `kern_inode_unref(inode)`，计数归零时 `free(inode)`。
  3. 为测试需要，在 `kern_vfs.h` 中新增 test-only 声明（用 `#ifdef NATIVE_TEST` 或内部头）：
     - `uint32_t kern_vfs_inode_ref_count(const kern_inode_t *inode);`
  4. **先写失败测试**：
     - `InodeRefCountOnRegister`：`kern_dentry_register` 后引用计数为 1。
     - `OpenIncrementsRefCount`：`kern_open` 后引用计数为 2（dentry + file）。
     - `UnlinkWhileOpenKeepsInode`：`kern_open` 后 `kern_vfs_unlink`，引用计数仍 ≥1，FD 仍可读写。
     - `CloseAfterUnlinkFreesInode`：关闭 FD 后引用计数归 0，再次 `kern_open` 同一路径返回 `KERN_ENOENT`。
  5. 实现：
     - `kern_dentry_register` 对 inode 取引用；替换旧 inode 时先 `kern_inode_unref(old)`。
     - `kern_open` 成功后对 inode 取引用。
     - `kern_close` 调用 `kern_inode_unref(f->inode)`。
     - `kern_vfs_unlink` 移除 dentry 后对 inode 放引用；计数为 0 则释放 inode。
     - `kern_vfs_touch` 保持分配新 inode 并通过 `kern_dentry_register` 建立引用。
  6. 验证。
  7. `git commit`。
- **回滚策略**：移除引用计数字段与 API，恢复 `unlink` 中 `free(dentry)` 的简单实现。
- **验收标准**：
  - 4 个新增测试通过
  - 全部 VFS 测试通过
  - `m5stick-c` 编译成功

---

### T5：K-P1-01 — 统一 API 返回类型为 `kern_err_t`
- **目标问题**：`kern_sched_class_register`、`procfs_register_file`、设备 ops 等仍返回 `int`
- **变更文件**：
  - `src/kernel/kern_sched_class.c` / `.h`
  - `src/kernel/kern_procfs.c`
  - `src/kernel/kern_device.h`
  - `src/kernel/kern_device.c`
  - `src/kernel/devices/dev_*.c` / `.h`
  - `test/test_native/test_kernel_sched.cpp`
  - `test/test_native/test_kernel_device.cpp`
- **步骤**：
  1. 用 Grep 列出所有应返回错误码但签名仍为 `int` 的内核 API（限定 sched/procfs/device ops）。
  2. **先写失败测试**：
     - `SchedClassRegisterReturnsEinval`：`kern_sched_class_register(NULL) == KERN_EINVAL`。
     - 设备相关测试增加对 `kern_err_t` 返回值的编译时/运行时断言。
  3. 修改签名与实现：
     - `kern_sched_class_register` 改为 `kern_err_t`，返回 `KERN_OK` / `KERN_EINVAL` / `KERN_ENOSPC`。
     - `procfs_register_file` 改为 `static kern_err_t`。
     - `kern_device_ops_t` 中 `open/close/read/write/ioctl` 改为 `kern_err_t`。
     - 更新 `kern_device.c` 的 VFS bridge，保持返回值语义不变。
     - 更新所有 `devices/dev_*.c` 实现。
  4. 验证。
  5. `git commit`。
- **回滚策略**：回退类型修改。
- **验收标准**：
  - 新增测试通过
  - 全部 kernel 测试通过
  - `m5stick-c` 编译成功

---

### T6：K-P1-07 — 资源追踪链表加锁
- **目标问题**：`kern_resource.c` 链表操作未对 `task->resource_head` 加锁
- **依赖**：建议排在 T2 之后，减少 `kern_resource.c` 冲突
- **变更文件**：
  - `src/kernel/kern_task.h`
  - `src/kernel/kern_resource.c`
- **步骤**：
  1. 在 `kern_task_t` 中新增 `volatile bool resource_lock` 字段（避免引入 `kern_sync.h` 造成循环包含）。
  2. **先写失败测试**：
     - `ResourceLockInitialized`：`kern_spawn` 后新任务 `resource_lock == false`。
  3. 在 `kern_resource.c` 中新增宏：
     - SMP：`_resource_lock(task)` / `_resource_unlock(task)` 使用 `__sync_lock_test_and_set` / `__sync_lock_release`
     - 单核：空操作
  4. 在 `kern_resource_track` / `kern_resource_untrack` / `kern_resource_release_all` 中包裹临界区。
  5. 验证。
  6. `git commit`。
- **回滚策略**：移除锁字段与临界区。
- **验收标准**：
  - 新增测试通过
  - 全部资源测试通过
  - `m5stick-c` 编译成功（`CONFIG_SMP_ENABLED` 路径需实际自旋锁生效）

---

### T7：K-P1-08 — TCB `scheduler_class_id` 初始化与同步
- **目标问题**：`scheduler_class_id` 未显式初始化；任务入队/出队时未同步
- **变更文件**：
  - `src/kernel/kern_sched_class.h` / `.c`
  - `src/kernel/kern_sched_rr.c`
  - `src/kernel/kern_sched_fifo.c`
  - `src/kernel/kern_task.h`
  - `src/kernel/kern_task_lifecycle.c`
  - `test/test_native/test_kernel_sched.cpp`
- **步骤**：
  1. 在 `kern_sched_class_t` 中新增 `int8_t class_id`。
  2. `kern_sched_class_register` 中设置 `cls->class_id = g_sched_class_count`（注册前）。
  3. **先写失败测试**：
     - `SpawnInitializesSchedulerClassId`：新任务 `scheduler_class_id` 不等于未初始化的 `0`，等于默认 RR class id。
     - `EnqueueSetsClassId`：对独立测试任务调用 `sched_class_rr.enqueue(task)` 后 `scheduler_class_id` 更新。
     - `DequeueClearsClassId`：调用 `sched_class_rr.dequeue(task)` 后 `scheduler_class_id == -1`。
  4. 实现：
     - 三个 `kern_spawn` 路径均显式设置 `task->scheduler_class_id = -1`，随后设置默认值（RR class id）。
     - `sched_rr_enqueue` / `sched_fifo_enqueue` 设置 `task->scheduler_class_id = cls->class_id`。
     - `sched_rr_dequeue` / `sched_fifo_dequeue` 设置 `task->scheduler_class_id = -1`。
  5. 验证。
  6. `git commit`。
- **回滚策略**：移除 `class_id` 字段与相关赋值。
- **验收标准**：
  - 3 个新增测试通过
  - 调度测试全部通过
  - `m5stick-c` 编译成功

---

### T8：K-P2-03 — 删除 `kern_sched_rr.c` 未使用的 `s_rr_last_prio`
- **目标问题**：死代码
- **变更文件**：
  - `src/kernel/kern_sched_rr.c`
- **步骤**：
  1. 删除 `static uint8_t s_rr_last_prio = 0;` 及其注释。
  2. 运行 `pio test -e native` + `pio run -e m5stick-c`。
  3. `git commit`。
- **回滚策略**：恢复变量声明。
- **验收标准**：
  - 编译无警告
  - 全部调度测试通过

---

## 3. 依赖关系图

```
T1 (FD 命名空间) ─┬─ 可并行 ─┬─ T4 (inode 引用计数)
                 │          ├─ T5 (返回类型统一)
                 │          ├─ T7 (scheduler_class_id)
                 │          └─ T8 (删除死代码)
                 │
T2 (资源节点分配器) ──→ T3 (栈归属任务)
                 │
                 └──→ T6 (资源链表加锁)
```

- **必须串行**：T2 → T3；T2 → T6（减少 `kern_resource.c` 合并冲突）
- **建议串行**：T1 优先执行，因为它改动 `kern_task_t` 且影响 VFS/Shell/测试，早做可减少后续合并风险
- **可并行**：T4、T5、T7、T8 彼此独立

---

## 4. 风险与回退点

| 子任务 | 风险等级 | 主要风险 | 回退方式 |
|--------|----------|----------|----------|
| T1 | **高** | FD 表移入 TCB 后，`kern_close`/`fd_release` 等跨任务语义易错；Shell/测试大量调用 `kern_open` | 单次 commit 后若 30 分钟内无法修复测试，执行 `git revert HEAD` |
| T3 | **高** | 栈在任务退出时被 `release_all` 释放，随后仍可能进行上下文切换，若 `stack_base` 未清空或 `reap_zombies` 仍 `free` 会导致 double-free/use-after-free | 恢复 `malloc` + 手动 `free` |
| T4 | **中** | inode 引用计数遗漏会导致打开文件期间 unlink 后悬空，或替换 inode 时重复释放 | 恢复无引用计数实现 |
| T5 | **中** | 返回类型改动面广，设备桥接层可能出现符号不匹配 | 回退类型签名；分设备逐步修改 |
| T2/T6/T7/T8 | 低 | 局部改动 | `git revert` 即可 |

**最大风险任务**：T1 与 T3。  
**统一回退点**：每个子任务独立 commit，任意时刻可 `git reset --hard HEAD~1` 回到上一子任务的绿色基线。

---

## 5. 测试策略

### 新增/修改的测试文件
- `test/test_native/test_kernel_vfs.cpp`：FD 隔离、`kern_open` 失败回滚、inode 引用计数
- `test/test_native/test_kernel_kmalloc.cpp`：`kern_kmalloc_untracked` API
- `test/test_native/test_kernel_stack.cpp`：栈归属任务、栈随任务释放
- `test/test_native/test_kernel_resource.cpp`：资源锁初始化（可复用现有文件）
- `test/test_native/test_kernel_sched.cpp`：`scheduler_class_id` 初始化与入队/出队同步
- `test/test_native/test_kernel_device.cpp`：设备 ops 返回类型

### 边界条件覆盖
- **FD**：父任务占满 FD 后子任务仍能打开；`fops->open` 失败后同一槽位可立即复用
- **资源节点**：`kern_kmalloc_untracked` 不进入 `resource_head`，避免递归
- **栈**：子任务栈在子任务资源链表中，不在父任务中；任务退出后栈自动释放
- **inode**：`open` 引用、`unlink` 不释放仍有引用的 inode、`close` 后最终释放、替换 inode 时旧 inode 正确释放
- **返回类型**：`NULL` 参数返回 `KERN_EINVAL`
- **调度类 ID**：初始 `-1`，默认 RR，enqueue 更新，dequeue 清 `-1`
- **并发**：SMP 路径下资源链表自旋锁不造成死锁（单核无操作，主要保证 ESP32 编译通过）

### 验证命令（每个子任务必须执行）
```bash
pio test -e native
pio run -e m5stick-c
```

---

## 6. 本轮处理与延后问题 ID 汇总

- **本轮处理**：K-P0-01、K-P0-02、K-P0-03、K-P0-04、K-P1-01、K-P1-03、K-P1-07、K-P1-08、K-P2-03
- **延后处理**：K-P1-02、K-P1-04、K-P1-05、K-P1-06、K-P2-01、K-P2-02、K-P2-04
