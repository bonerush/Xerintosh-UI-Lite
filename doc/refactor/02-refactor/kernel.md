# 内核层重构报告（2026-06-15）

## 变更摘要

| 变更 | 文件 | 诊断 ID | 效果 |
|------|------|---------|------|
| O(1) 尾追加 enqueue | `kern_sched_class.h`, `kern_sched_rr.c`, `kern_sched_fifo.c`, `kern_sched.c` | K01 | enqueue O(n)→O(1) |
| FD 对象池 | `kern_vfs.c` | K03 | kern_open() 无 malloc |

## 详细变更

### 1. 调度器 O(1) enqueue

**问题**：`sched_rr_enqueue()` 每次遍历整个单链表寻找尾节点（O(n)），在任务数多时开销显著。

**修复**：
- `kern_sched_class.h:42`：添加 `task_list_tail` 字段
- `kern_sched_rr.c:35-53`：enqueue 使用 `task_list_tail` 指针 O(1) 追加，含 NULL tail 防御回退
- `kern_sched_rr.c:57-83`：dequeue 移除尾节点时同步更新 `task_list_tail`
- `kern_sched_fifo.c:51-73`：FIFO dequeue 同样维护 tail 指针
- `kern_sched.c:98,140,206`：三处 init 路径均设置 `task_list_tail`

### 2. FD 对象池

**问题**：每次 `kern_open()` 调用 `calloc()` 分配 `kern_file_t`（28 bytes），close 时 `free()`。频繁 open/close 导致堆碎片。

**修复**：
- `kern_vfs.c:27-56`：预分配 `g_fd_pool[16]` + 位图 `g_fd_pool_bitmap`
- `fd_pool_alloc()`：O(n) 位扫描（n≤16），memset 清零
- `fd_pool_free()`：O(1) 位清除
- `fd_alloc()`：`calloc()` → `fd_pool_alloc()`，池耗尽返回 `KERN_ENOMEM`
- `fd_close_raw()`：`free()` → `fd_pool_free()`
- `kern_open()` 错误回退路径同步使用 `fd_pool_free()`

## 内存影响

| 指标 | 变更前 | 变更后 |
|------|--------|--------|
| FD 池静态占用 | 0 | 16 × 28 = 448 bytes |
| 每次 kern_open malloc | 28+16=44 bytes 堆 | 0 |
| task_list_tail 字段 | 0 | 4 bytes/class × 2 = 8 bytes |

权衡：用 456 bytes 静态内存换取消除堆分配开销和碎片风险。

## 验证

- 硬件构建：✅ SUCCESS（RAM 22.3%, Flash 88.1%）
- Native 测试：✅ 414/415 通过
- 调度器测试：✅ RrEnqueueSetsClassId, RrDequeueClearsClassId, FifoEnqueueSetsClassId, FifoDequeueClearsClassId 全部通过
- VFS 测试：✅ 所有 VFS 测试通过（含 FD 池耗尽场景）

## 已知暂缓

- K02（pick_next 双遍扫描优化）：需独立 wake_list，影响较大，本轮暂缓
- K05（命令历史压缩）：P1 但为 UI 无关改动，后续独立处理
- K06（结构体重排去死字段）：P1，需影响多处代码，独立 PR

## 变更文件列表

| 文件 | 变更行数 |
|------|----------|
| `src/kernel/kern_sched_class.h` | +1 |
| `src/kernel/kern_sched_rr.c` | ~15 |
| `src/kernel/kern_sched_fifo.c` | ~12 |
| `src/kernel/kern_sched.c` | +3 |
| `src/kernel/kern_vfs.c` | +36 |
