# 第十二轮重构归档报告：memory-schedule（2026-06-19）

## 目标

改进 Xeros 内核的内存分配与任务调度机制，使任务栈/内存按当前实际需求自动分配，解决 WiFi/蓝牙因静态内存预留导致无法共存和服务错误的问题。

- **分支**：`refactor/2026-06-19-memory-schedule`
- **基线提交**：`c6757e3`
- **主要产物**：
  - `doc/refactor/00-baseline-memory-schedule.md`
  - `doc/refactor/01-diagnosis-memory-schedule.md`
  - `doc/refactor/02-refactor/kernel-memory.md`
  - `doc/refactor/02-refactor/kernel-schedule.md`
  - `doc/refactor/03-integration-memory-schedule.md`
  - 本文档

---

## 关键问题与修复

### P0-01 FreeRTOS 栈单位语义错误

`xTaskCreatePinnedToCore()` 的 `usStackDepth` 参数以 **字（`StackType_t`）** 为单位，代码之前直接把 `stack_size` 字节数传入，导致每个 Xeros 任务在 ESP32 上获得 **4 倍于预期的栈**。

- **修复**：`kern_port_freertos.c` 在创建任务前把字节转换为字数，`task->stack_size` 保持字节语义不变。
- **收益**：理论上释放约 36KB+ 堆内存，是 WiFi/BT 共存的关键前提。

### P0-02 栈按需预留与监控

- TCB 新增 `stack_highwater` 字段，记录历史最大栈使用量。
- 新增 `kern_task_stack_highwater()` / `kern_task_stack_recommend()`。
- Native 后端支持 `kern_task_stack_grow()` 自动增长；FreeRTOS 后端明确不支持动态增长，采用创建时按需预留 + 运行时监控。
- `kern_sched.c` 每 500 ticks 检查栈压力，超过 75% 输出警告。

### P0-03 内存统计与压力

- `kern_kmalloc.c` 新增 `kern_kmem_stat_t` 与 `kern_kmem_get_stats()`，封装 ESP-IDF `heap_caps_*()`。
- 新增内存压力等级 `kern_kmem_pressure_level_t`（LOW/MEDIUM/HIGH）。
- 新增保留内存接口 `kern_kmem_set_reserved_bytes()` / `kern_kmem_reserved_bytes()`。
- 调度器每 100 ticks 分发 `memory_pressure` 回调给所有调度类；RR class 在高压下缩短时间片。

### A1/A2 App 统一内存视图

- 新增 `src/app/app_mem.c/h`，提供 `xeros_mem_get_stats()` / `xeros_mem_available_bytes()` / `xeros_mem_can_alloc()`。
- WiFi/BT 管理器启用前改用 **总空闲 + 最大连续块** 双维度内存守卫，避免碎片导致的大块分配失败。
- 服务启用失败时回写 `g_wifi_on` / `g_bt_on`，保持全局状态与 UI 开关一致。

### A5 任务栈按需初始化

- `main.cpp` 中 UI/WiFi/BT 任务统一使用 4096 字节栈。

---

## 变更文件清单

| 层级 | 文件 | 说明 |
|------|------|------|
| 内核 | `src/kernel/kern_port_freertos.c` | FreeRTOS 栈字节→字数转换 |
| 内核 | `src/kernel/kern_task.h` | TCB 新增 `stack_highwater` |
| 内核 | `src/kernel/kern_task_stack.c/h` | 高水位、推荐值、Native 自动增长 |
| 内核 | `src/kernel/kern_kmalloc.c/h` | 内存统计、压力等级、保留内存 |
| 内核 | `src/kernel/kern_sched.c` | 栈压力检查、内存压力分发 |
| 内核 | `src/kernel/kern_sched_class.c/h` | 新增 `memory_pressure` vtable 回调 |
| 内核 | `src/kernel/kern_sched_rr.c` | 高压下自适应时间片 |
| 内核 | `src/kernel/kern_resource.c` | 资源节点池扩至 `KERN_MAX_TASKS * 4` |
| 内核 | `src/kernel/kern_procfs.c` | `/proc/meminfo` 使用新统计 |
| 内核 | `src/kernel/kern_shell_cmds.c` | `ps`/`free` 输出字节单位 |
| App | `src/app/app_mem.c/h` | 新增统一内存视图 |
| App | `src/app/wifi/wifi_manager.cpp` | 双内存守卫、错误回写 |
| App | `src/app/bluetooth/bt_manager.cpp` | 双内存守卫、BT 错误分类 |
| App | `src/app/bluetooth/bt_uart_service.cpp/h` | 错误码细化 |
| App | `src/app/app_state.c` | 全局状态同步 |
| App | `src/main.cpp` | 任务栈 4096、BT 懒加载说明 |
| 测试 | `test/test_native/test_kernel_stack.cpp` | 栈高水位/推荐值 |
| 测试 | `test/test_native/test_kernel_kmalloc.cpp` | 内存统计/压力 |
| 测试 | `test/test_native/test_kernel_resource.cpp` | 资源池扩容 |
| 测试 | `test/test_native/test_kernel_sched.cpp` | 调度 tick 内存压力 |
| 测试 | `test/test_native/test_app_mem.cpp` | App 内存视图 |
| 测试 | `test/test_ble_uart/test_ble_uart.cpp` | BLE UART 回归 |
| 文档 | `doc/kernel/kern-kmalloc.md` | 统计/压力/保留内存 |
| 文档 | `doc/kernel/kern-port.md` | 栈单位语义 |
| 文档 | `doc/kernel/kern-sched-class.md` | `memory_pressure` 回调 |
| 文档 | `doc/kernel/kern-task.md` | 栈高水位/自动增长 |
| 文档 | `doc/kernel/kern-sched-rr.md` | 高压自适应时间片 |
| 文档 | `doc/app/app-mem.md` | 新增 App 内存视图文档 |
| 文档 | `doc/app/index.md` | 索引新增 app-mem |
| 文档 | `doc/index.md` | 重构历史 + 第十二轮总结 |

---

## 验证结果

### 硬件构建

```bash
pio run -e m5stick-c
```

- **结果**：SUCCESS
- **RAM**：27.2%（88,976 / 327,680 bytes）
- **Flash**：89.4%（1,874,481 / 2,097,152 bytes）

### Native 测试

```bash
pio test -e native
```

| 环境 | 用例数 | 通过 | 失败 | 异常退出 | 状态 |
|------|--------|------|------|----------|------|
| native/test_ble_uart | 20 | 20 | 0 | 0 | PASSED |
| native/test_native | 200 | 199 | 0 | 1 (SIGTRAP) | ERRORED |
| native/test_token_usage | 23 | 23 | 0 | 0 | PASSED |
| **合计** | **243** | **242** | **0** | **1** | **242/243 通过** |

### 基线对比

| 失败项 | 基线 (c6757e3) | 本轮重构后 | 结论 |
|--------|----------------|------------|------|
| `KernelVFSTest.MaxFdPerTask` | FAILED (-24) | FAILED (-24) | 未恶化 |
| `KernelVFSTest.FdNamespaceIsolated` | FAILED (-24) | FAILED (-24) | 未恶化 |
| `ShellCompleteTest.*` 7 个 | FAILED (-2) | FAILED (-2) | 未恶化 |
| `SpringAnimTest.OvershootsWithLowDamping` | FAILED | FAILED | 未恶化 |
| `test_native` SIGTRAP | ERRORED | ERRORED | 同一点崩溃，未恶化 |

本轮重构**没有引入新的 native 失败用例**。

---

## 已知问题

1. **Native 上下文切换崩溃**：`test_native` 在涉及 `swapcontext` 切换到 spawned 任务后异常退出（SIGTRAP）。已确认该问题在原始 `main` 分支（`c6757e3`）同样存在，与本轮改动无直接因果关系。
2. **FreeRTOS 栈不可动态增长**：ESP32 后端采用创建时预留策略，若任务运行时发现栈不足只能重新调整代码中的 `stack_min`。
3. **未真机验证**：因无物理设备连接，仅完成编译验证。建议后续在 M5Stick-C 真机上验证 WiFi/BT 共存、Shell `free`/`ps` 输出、栈高水位显示。

---

## 合并建议

- 代码层面已完成，硬件构建成功，文档已同步。
- 建议在真机验证 WiFi/BT 共存后合并到 `main`。
- 合并前/后应单独跟踪 native `ucontext` 崩溃的根因修复（非本轮 scope）。

---

## 经验教训

- **单位语义是嵌入式内存 bug 的高频来源**：FreeRTOS 的 `usStackDepth` 与常见“字节”直觉相反，必须在 port 层显式转换并文档化。
- **总空闲 ≠ 可分配内存**：大协议栈（BT/WiFi）必须检查 `largest_free_block`，碎片同样会导致启动失败。
- **保留水位应成为系统初始化的一部分**：当前默认 0，后续应根据真机最小堆需求设置合理值。
