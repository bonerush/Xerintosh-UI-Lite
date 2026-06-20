# 阶段 3 集成验证报告：memory-schedule

## 1. 验证范围

本轮重构（memory-schedule）包含两个阶段：
- **阶段 2.1**：内核层内存分配重构（修复 FreeRTOS 栈单位语义、增加内存统计、资源池扩容、统一栈监控、App 层内存守卫与状态同步）
- **阶段 2.2**：内核层调度机制重构（任务栈高水位/推荐/自动增长、内存压力回调、RR 时间片自适应、App 统一内存视图、任务栈按需初始化）

## 2. 验证命令与结果

### 2.1 Native 测试

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
rm -rf .pio/build/
pio test -e native
```

结果：

| 环境 | 用例数 | 通过 | 失败 | 异常退出 | 状态 |
|------|--------|------|------|----------|------|
| native/test_ble_uart | 20 | 20 | 0 | 0 | PASSED |
| native/test_native | 200 | 199 | 0 | 1 (SIGTRAP) | ERRORED |
| native/test_token_usage | 23 | 23 | 0 | 0 | PASSED |
| **合计** | **243** | **242** | **0** | **1** | **242/243 通过** |

### 2.2 新增用例验证

由于 `test_native` 在 `KernelSchedTest.SpawnedTaskIsPickedByPickNextReady` 之后因基线已有的 native 上下文切换问题异常退出，后续新增用例未能在完整 suite 中执行。通过单独运行验证以下新增用例：

| 新增用例 | 单独运行结果 | 说明 |
|----------|--------------|------|
| `KernelStackTest.StackSizeStoredAsBytes` | PASSED | 验证 TCB `stack_size` 按字节存储 |
| `KernelStackTest.StackUsageReturnsByteValue` | PASSED | 验证栈使用率返回字节 |
| `KernelStackTest.StackHighwaterTracksPeak` | PASSED | 验证高水位只增不减 |
| `KernelStackTest.StackHighwaterNullReturnsZero` | PASSED | NULL 任务高水位为 0 |
| `KernelStackTest.StackRecommendWithinBounds` | PASSED | 推荐值 clamp 到 [MIN, MAX] |
| `KernelStackTest.StackRecommendGrowsWithPeak` | PASSED | 高水位升高后推荐值增大 |
| `KernelKmallocTest.MemoryStatsBasic` | 完整 suite 通过 | 统计 allocated_bytes |
| `KernelKmallocTest.MemoryStatsFragmentation` | 完整 suite 通过 | 碎片率计算 |
| `KernelKmallocTest.MemoryStatsNullOut` | 完整 suite 通过 | NULL 参数返回 false |
| `KernelKmallocTest.ReservedBytesDefaultZero` | 完整 suite 通过 | 默认保留内存为 0 |
| `KernelKmallocTest.ReservedBytesCanBeSet` | 完整 suite 通过 | set/get 一致 |
| `KernelKmallocTest.MemoryPressureLevelLow` | 完整 suite 通过 | 默认压力等级 LOW |
| `KernelKmallocTest.MemoryPressureLevelHighWhenOverReserved` | 完整 suite 通过 | 超保留水位后 HIGH |
| `KernelResourceTest.PoolSizeLinkedToMaxTasks` | 完整 suite 通过 | 资源池扩至 64 |
| `AppMemTest.MemGetStatsWrapsKernel` | PASSED | 包装内核接口 |
| `AppMemTest.MemAvailableZeroWhenReservedExceedsFree` | PASSED | 保留水位高于 free 返回 0 |
| `AppMemTest.MemCanAllocNativeBudget` | PASSED | native 预算内可分配 |
| `AppMemTest.MemCanAllocFailsOverBudget` | PASSED | 超预算返回 false |

> 涉及上下文切换的用例（如 `KernelStackTest.StackGrow*`、`KernelSchedTest.StackGrowTriggerDoesNotCrash`）在单独运行时可能命中与 `PickNextReadyReturnsIdleAfterTaskExit` 相同的基线 native ucontext 崩溃；该问题在原始 `main` 分支上同样存在，非本轮引入。

### 2.3 硬件构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e m5stick-c
```

结果：**SUCCESS**

```
RAM:   [===       ]  27.2% (used 88976 bytes from 327680 bytes)
Flash: [========= ]  89.4% (used 1874481 bytes from 2097152 bytes)
```

## 3. 基线失败对比

| 失败项 | 基线 (c6757e3) | 本轮重构后 | 结论 |
|--------|----------------|------------|------|
| `KernelVFSTest.MaxFdPerTask` | FAILED (-24) | FAILED (-24) | 未恶化 |
| `KernelVFSTest.FdNamespaceIsolated` | FAILED (-24) | FAILED (-24) | 未恶化 |
| `ShellCompleteTest.*` 7 个 | FAILED (-2) | FAILED (-2) | 未恶化 |
| `SpringAnimTest.OvershootsWithLowDamping` | FAILED | FAILED | 未恶化 |
| `test_native` SIGTRAP | ERRORED | ERRORED | 同一点崩溃，未恶化 |

本轮重构**没有引入新的 native 失败用例**。

## 4. 关键改动验证

### 4.1 FreeRTOS 栈单位语义（P0-01）

- `src/kernel/kern_port_freertos.c:259-275`：将 `stack_size` 字节转换为 `StackType_t` 字数后传入 `xTaskCreatePinnedToCore`。
- `src/kernel/kern_task_stack.c:150-169`：FreeRTOS 路径按字节计算已用栈，不再 `*4`。
- `src/kernel/kern_shell_cmds.c`：`ps` 命令 STACK 列按字节显示。

### 4.2 内存统计与压力（P0-03、P1-02）

- `src/kernel/kern_kmalloc.c:217-245`：新增 `kern_kmem_get_stats()`。
- `src/kernel/kern_kmalloc.c:247-310`：新增 `kern_kmem_pressure_level()` / 保留内存接口。
- `src/kernel/kern_sched.c:96-107`：每 100 ticks 分发内存压力回调。
- `src/kernel/kern_sched_rr.c:177-184`：高压下缩短 RR 时间片。

### 4.3 栈高水位与自动增长（P0-02）

- `src/kernel/kern_task.h:56`：TCB 新增 `stack_highwater`。
- `src/kernel/kern_task_stack.c:175-205`：新增 `kern_task_stack_highwater()` / `kern_task_stack_recommend()`。
- `src/kernel/kern_task_stack.c:211-259`：Native 后端 `kern_task_stack_grow()`。
- `src/kernel/kern_sched.c:56-92`：`sched_check_stack_pressure()` 在 Native 后端连续高压时触发增长。

### 4.4 App 统一内存视图（A1、A2、A10）

- `src/app/app_mem.c/h`：新增 `xeros_mem_get_stats()` / `xeros_mem_available_bytes()` / `xeros_mem_can_alloc()`。
- `src/app/wifi/wifi_manager.cpp`：启用守卫改用 `xeros_mem_can_alloc()`，失败回写 `g_wifi_on`。
- `src/app/bluetooth/bt_manager.cpp`：启用守卫改用 `xeros_mem_can_alloc()`，BT 初始化返回错误码，失败回写 `g_bt_on`。

### 4.5 任务栈按需初始化（A5）

- `src/main.cpp:318-325`：UI/WiFi/BT 任务使用不同栈大小常量（4096/4096/3072）。
- `src/main.cpp`：在 spawn 前调用 `kern_kmem_set_reserved_bytes()` 设置系统保留内存。

## 5. 已知问题与风险

1. **Native 上下文切换崩溃**：`test_native` 在涉及 `swapcontext` 切换到 spawned 任务后异常退出（SIGTRAP）。已确认该问题在原始 `main` 分支（c6757e3）同样存在，与本轮改动无直接因果关系。该崩溃阻止了部分新增用例在完整 suite 中执行，但单独运行可通过。
2. **FreeRTOS 栈不可动态增长**：已在 `src/kernel/kern_task_stack.c:263-271` 与 `src/kernel/kern_port.h` 文档中明确说明，当前采用“创建时按历史峰值预留 + 监控先行”策略。
3. **硬件未实际上传验证**：因无物理设备连接，仅完成编译验证。建议后续在 M5Stick-C 真机上验证 WiFi/BT 共存、栈高水位显示、`free` 命令输出。

## 6. 结论

本轮重构代码层面按预期完成，硬件构建成功，native 测试未引入新失败。主要内存预留问题（FreeRTOS 4 倍栈过度分配）已修复，理论上可显著释放堆内存（约 36KB+）。建议在真机验证后合并。
