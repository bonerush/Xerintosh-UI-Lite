# 内核层深度重构报告 v2（第二轮 kernel-deep）

**日期**: 2026-06-15  
**分支**: `refactor/2026-06-15-kernel-ui`  
**上轮报告**: [kernel.md](kernel.md)（O(1) enqueue + FD 对象池 + P0 ISR 修复）

## 变更摘要

| 变更 | 文件 | 诊断 ID | 效果 |
|------|------|---------|------|
| 移除重复 kern_fd_t typedef | `kern_shell_cmds.h` | P1-11 | 消除编译歧义风险 |
| O(1) 任务插入（尾指针） | `kern_task_lifecycle.c`, `kern_sched.c/h` | — | kern_spawn() 不再 O(n) 遍历 |
| 资源节点对象池 (32) | `kern_resource.c` | — | 消除 kern_resource_track 中 malloc |
| 设备 buf NULL 加固 | `dev_fb0/input0/pwrkey/ttyS0` | P1-10 | 防止空指针解引用 |

## 详细变更

### 1. 移除重复 kern_fd_t typedef（P1-11）

**问题**：`kern_types.h` 和 `kern_shell_cmds.h` 各自定义了 `typedef int16_t kern_fd_t;`，
可能导致 ODR 违规和编译歧义。

**修复**（`src/kernel/kern_shell_cmds.h:18-22`）：
- 删除重复 `typedef int16_t kern_fd_t;`
- 类型定义统一由 `kern_types.h` 提供

### 2. O(1) 任务插入（尾指针）

**问题**：`kern_spawn()` 每次追加新任务到全局链表 `g_task_list` 时，
需 O(n) 遍历找到尾节点。虽然当前任务数较少（~10），但每次 spawn 都需扫描
整个链表（包括 ZOMBIE 节点）。

**修复**：
- `kern_sched.h:28`：新增 `extern kern_task_t *g_task_list_tail;`
- `kern_sched.c:34`：定义并初始化为 NULL
- `kern_sched.c:62,158,210`：三处 `kern_sched_init()` 路径中尾部重置
- `kern_task_lifecycle.c:94-116, 240-263`：spawn 追加使用尾指针 O(1)：
  ```c
  if (g_task_list_tail != NULL) {
      g_task_list_tail->next = task;   // O(1) 追加
  } else {
      // 尾指针未初始化：回退 O(n) 遍历（仅首次 spawn）
      kern_task_t *t = g_task_list;
      while (t->next != NULL) t = t->next;
      t->next = task;
  }
  g_task_list_tail = task;  // 更新尾指针
  ```
- `kern_task_lifecycle.c:530-535`：`reap_zombies()` 回收后重建尾指针（O(n)，低频）

### 3. 资源节点对象池

**问题**：`kern_resource_track()` 每次调用 `kern_kmalloc_untracked(sizeof(kern_resource_t))`
分配 16 字节节点。`kern_open()` 每次打开文件也调用此函数注册资源。
频繁 open/close 导致堆碎片。

**修复**（`src/kernel/kern_resource.c:8-65`）：
- 预分配 `g_res_pool[32]`（32 × 16 = 512 bytes）+ 位图
- `res_pool_alloc()`：O(n) 位扫描（n=32），memset 清零后返回
- `res_pool_free()`：O(1) 位清除
- `res_is_pooled()`：通过地址范围判断是否为池节点
- `kern_resource_track()`：优先池分配，池耗尽回退 `kern_kmalloc_untracked`
- `res_node_free()`：统一释放逻辑，池节点归还池，堆节点 `kern_kfree_untracked`
- `kern_resource_untrack()` / `kern_resource_release_all()`：统一使用 `res_node_free()`

### 4. 设备驱动 buf NULL 加固（P1-10）

**问题**：多个设备驱动的 `read()`/`write()` 实现直接对 `buf` 参数做类型转换
（`(uint8_t*)buf`、`(char*)buf`），未检查 NULL。当上层误传入 NULL 时导致
空指针解引用。

**修复**（5 个驱动，6 个函数）：
| 文件 | 函数 | 添加检查 |
|------|------|----------|
| `dev_input0.c:33` | `dev_input0_read()` | `if (buf == NULL) return KERN_EINVAL;` |
| `dev_pwrkey.c:33` | `pwrkey_read()` | 同上 |
| `dev_ttyS0.cpp:65` | `dev_ttyS0_read()` | 同上 |
| `dev_ttyS0.cpp:95` | `dev_ttyS0_write()` | 同上 |
| `dev_fb0.c:44` | `dev_fb0_write()` | 同上 |

## 内存影响

| 指标 | 变更前 | 变更后 |
|------|--------|--------|
| resource 池静态占用 | 0 | 32 × 16 = 512 bytes |
| 每次 track malloc | 16 bytes 堆 | 0（池内时） |
| task_list_tail 字段 | 0 | 4 bytes（全局指针） |
| 合计静态增加 | 0 | ~516 bytes |

权���：用 516 bytes 静态内存换取消除运行时堆分配和碎片风险。

## 验证

- 硬件构建：✅ SUCCESS（RAM 25.5%, Flash 88.5%）
- Native 测试：✅ 414/415 通过，1 skipped
- 调度器测试：✅ RrEnqueueClassId, RrDequeueClassId, FifoEnqueueClassId, FifoDequeueClassId 全部通过
- VFS 测试：✅ 全部 VFS 测试通过
- 任务测试：✅ SpawnRegistersTaskInList 通过
- 无新增编译警告

## 变更文件清单

| 文件 | 变更 |
|------|------|
| `src/kernel/kern_shell_cmds.h` | -1（删除重复 typedef） |
| `src/kernel/kern_sched.h` | +1（extern tail pointer） |
| `src/kernel/kern_sched.c` | +4（定义 + 3 处 init 重置） |
| `src/kernel/kern_task_lifecycle.c` | ~25（O(1) 尾追加 + zombie 重建） |
| `src/kernel/kern_resource.c` | +58（对象池 + res_node_free + string.h） |
| `src/kernel/devices/dev_input0.c` | +1（buf NULL 检查） |
| `src/kernel/devices/dev_pwrkey.c` | +1（buf NULL 检查） |
| `src/kernel/devices/dev_ttyS0.cpp` | +2（read + write NULL 检查） |
| `src/kernel/devices/dev_fb0.c` | +1（write NULL 检查） |
