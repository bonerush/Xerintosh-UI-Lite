# 阶段 1 诊断报告：memory-schedule（内核内存分配与调度）

## 1. 诊断结论总览

本轮重构目标是让 Xeros 内核的任务栈/内存按当前实际需求自动分配，解决 WiFi/蓝牙因静态内存预留导致无法共存和服务错误的问题。

通过对内核层与 App 层源码的只读扫描，确认**当前最关键的根因是 FreeRTOS 后端的任务栈单位语义不一致**：

- 公共 API `kern_spawn(name, entry, arg, stack_min)` 的文档说明 `stack_min` 为**字节**；
- FreeRTOS 后端 `kern_port_freertos.c:259` 直接将该值作为 `xTaskCreatePinnedToCore(..., usStackDepth, ...)` 的参数，而 `usStackDepth` 在 FreeRTOS 中的单位是 **字**（`StackType_t`，ESP32 上为 4 字节）；
- 因此 App 请求 4096 字节栈时，实际分配了 **4096 字 = 16 KB**，存在 **4 倍过度预留**。

在 ESP32-PICO 仅有 520 KB SRAM 的条件下，UI/WiFi/BT/shell 等任务各 16 KB，合计超过 64 KB 栈空间被过度占用，导致 WiFi/蓝牙协议栈初始化时可用连续堆不足，从而触发 BT `begin()` 失败或 SPP 服务错误。

其他关键发现：

| 维度 | 结论 |
|------|------|
| 栈自动伸缩 | `KERN_STACK_GROW` / `KERN_STACK_MAX` 已定义但未被使用；canary/高水位只能告警，无法自动扩容 |
| 内存诊断 | `kmalloc` 只是 malloc 薄包装，缺少总空闲、最大连续块、碎片率等接口 |
| 调度器 | 调度类接口没有内存压力回调，无法根据内存状态调整时间片 |
| App 内存守卫 | BT/WiFi 只检查 `ESP.getFreeHeap()` 总空闲堆，未检查最大连续块；阈值硬编码 |
| BT 初始化 | 失败未区分内存不足/Bluedroid 状态错误/射频冲突；关闭 WiFi 后固定 delay 500ms 未轮询确认 |
| 状态同步 | `g_wifi_on` / `g_bt_on` 是用户意图，可能未反映驱动真实状态 |

## 2. 任务栈在三种后端下的分配现状

| 后端 | 栈选择逻辑 | 分配方式 | 监控 |
|------|-----------|----------|------|
| `NATIVE_TEST` | `(stack_min > 0) ? stack_min : KERN_STACK_MIN` | `kern_kmalloc_for_task` 从进程堆分配 | 每 500 ticks 检查使用率 >75% 告警 |
| `XEROS_NATIVE_SCHED` | 同 NATIVE_TEST | 同 NATIVE_TEST | **缺失** NATIVE_TEST 同等监控 |
| `FreeRTOS`（默认） | `(stack_min > 0) ? stack_min : KERN_PORT_STACK_MIN`，强制下限 4096 | `xTaskCreatePinnedToCore(..., stack_size, ...)` 把字节当字用 | `uxTaskGetStackHighWaterMark`，`kern_task_stack_usage()` 按 `task->stack_size * 4` 显示字节 |

> 关键证据：`kern_port_freertos.c:262` 注释明确写“栈大小（字）”，`kern_task_stack_usage()` 在 FreeRTOS 路径下又将 `task->stack_size` 乘以 4 转字节显示——这反向证明 `task->stack_size` 在该后端被内部视为**字**。

## 3. 内核层问题清单

### P0-01: FreeRTOS 后端任务栈单位语义不一致（4 倍过度预留）

| 项目 | 内容 |
|------|------|
| **ID** | P0-01 |
| **优先级** | P0 |
| **模块** | kernel/port、kernel/task |
| **文件/行** | `src/kernel/kern_task_lifecycle.c:219-223`；`src/kernel/kern_port_freertos.c:259-267`；`src/kernel/kern_task_stack.c:128-144`；`src/kernel/kern_shell_cmds.c:184-185` |
| **问题描述** | `kern_spawn()` 的 `stack_min` 文档为字节，但 FreeRTOS 后端直接作为 `usStackDepth`（字）传入，导致实际分配 4 倍栈 |
| **根因分析** | 端口层未在创建 FreeRTOS 任务前将字节转换为 `sizeof(StackType_t)` 字数；TCB `stack_size` 字段在不同后端含义不一致 |
| **建议重构动作** | 1. 统一公共 API 语义：`stack_min` 始终为字节；2. FreeRTOS 后端转换：`stack_words = stack_bytes / sizeof(StackType_t)`；3. TCB 统一按字节存储 `stack_size`；4. `kern_task_stack_usage()` / shell / procfs 统一按字节显示，不再 `*4` |
| **关联测试** | 新增 native 桩测试验证 `kern_spawn(..., 1024)` 在 FreeRTOS 路径实际分配 1024 字节；硬件验证 `/proc/tasks` 显示字节总量与请求一致 |

### P0-02: 任务栈缺乏自动增长/缩小机制

| 项目 | 内容 |
|------|------|
| **ID** | P0-02 |
| **优先级** | P0 |
| **模块** | kernel/task、kernel/mm |
| **文件/行** | `src/kernel/kern_types.h:91-93`；`src/kernel/kern_task_stack.c`；`src/kernel/kern_sched.c:244-254` |
| **问题描述** | `KERN_STACK_GROW=1024`、`KERN_STACK_MAX=8192` 已定义但未被使用；只能告警无法扩容 |
| **根因分析** | 没有栈重分配策略；FreeRTOS 栈由底层拥有，Xeros 无法直接 realloc |
| **建议重构动作** | 1. Native 后端实现 `task_stack_grow()`：分配新栈→复制有效栈区→更新上下文→释放旧栈；2. FreeRTOS 后端采用“创建时按历史峰值预留”策略，并新增 `kern_task_stack_recommend()`；3. 在 `kern_sched_tick` 中持续高使用率触发扩容请求 |
| **关联测试** | 新增 `KernelStackTest.AutoGrowWhenHighUsage` |

### P0-03: kmalloc 缺少内存压力/碎片诊断接口

| 项目 | 内容 |
|------|------|
| **ID** | P0-03 |
| **优先级** | P0 |
| **模块** | kernel/mm |
| **文件/行** | `src/kernel/kern_kmalloc.c/h` |
| **问题描述** | `kmalloc` 只是 malloc/free 追踪头，没有总空闲、最大连续空闲块、碎片率等统计 |
| **根因分析** | 分配头只记录 size/owner，没有全局统计变量 |
| **建议重构动作** | 1. 新增 `kern_kmem_stat_t` 与 `kern_kmem_get_stats()`；2. ESP32 后端封装 `heap_caps_get_free_size()` / `heap_caps_get_largest_free_block()`；3. 在 `/proc/meminfo` 和 shell 中暴露 |
| **关联测试** | 新增 `KernelKmallocTest.MemoryStats` |

### P1-01: 资源追踪节点池大小固定且偏小

| 项目 | 内容 |
|------|------|
| **ID** | P1-01 |
| **优先级** | P1 |
| **模块** | kernel/resource |
| **文件/行** | `src/kernel/kern_resource.c:23-26` |
| **问题描述** | `RES_POOL_SIZE=32` 静态数组，16 任务场景下易耗尽，回退 `kern_kmalloc_untracked` 增加碎片 |
| **建议重构动作** | 将池大小与 `KERN_MAX_TASKS` 关联，如 `KERN_MAX_TASKS * 4` |
| **关联测试** | 资源追踪集成测试 |

### P1-02: 调度器类接口无内存压力回调

| 项目 | 内容 |
|------|------|
| **ID** | P1-02 |
| **优先级** | P1 |
| **模块** | kernel/sched |
| **文件/行** | `src/kernel/kern_sched_class.h:30-43`；`src/kernel/kern_sched.c:229-323` |
| **问题描述** | 调度类只有 enqueue/dequeue/pick_next/tick/prio_changed，无 `memory_pressure` 回调 |
| **建议重构动作** | 1. 在 `kern_sched_class_t` 增加可选 `memory_pressure` 回调；2. `kern_sched_tick` 根据 `kern_kmem_get_stats()` 计算压力等级并调用；3. RR 在高压下缩短时间片 |
| **关联测试** | 新增 `KernelSchedTest.MemoryPressureCallback` |

### P1-03: `XEROS_NATIVE_SCHED` 栈监控缺失

| 项目 | 内容 |
|------|------|
| **ID** | P1-03 |
| **优先级** | P1 |
| **模块** | kernel/sched |
| **文件/行** | `src/kernel/kern_sched.c:244-254`（NATIVE_TEST 有）；`src/kernel/kern_sched.c:272-295`（XEROS_NATIVE_SCHED 无） |
| **问题描述** | XEROS_NATIVE_SCHED 分支没有 NATIVE_TEST 同等的 75% 栈使用率告警 |
| **建议重构动作** | 提取后端无关的 `sched_check_stack_pressure()`，在 NATIVE_TEST、XEROS_NATIVE_SCHED、FreeRTOS 后端统一调用 |
| **关联测试** | `test_kernel_sched.cpp` |

### P1-04: FreeRTOS 任务栈无法被 Xeros 影响或回收

| 项目 | 内容 |
|------|------|
| **ID** | P1-04 |
| **优先级** | P1 |
| **模块** | kernel/port |
| **文件/行** | `src/kernel/kern_port_freertos.c:240-275`；`src/kernel/kern_task_lifecycle.c:358-375` |
| **问题描述** | FreeRTOS 任务栈由 `xTaskCreatePinnedToCore()` 在 FreeRTOS 堆中分配，Xeros 创建后无法调整大小或影响分配策略 |
| **建议重构动作** | 1. 文档化限制；2. 实现“创建时按历史峰值预留”策略；3. 评估合并后台任务或启用 `XEROS_NATIVE_SCHED` |
| **关联测试** | 硬件集成测试 |

## 4. App 层问题清单

### A1: BT/WiFi 内存守卫只检查总堆，未检查最大连续块

| 项目 | 内容 |
|------|------|
| **ID** | A1 |
| **优先级** | P0 |
| **模块** | WiFi/BT 管理器 |
| **文件/行** | `src/app/bluetooth/bt_manager.cpp:106-112`；`src/app/wifi/wifi_manager.cpp:222-230` |
| **问题描述** | BT 检查 `ESP.getFreeHeap() < 70000`、WiFi 检查 `< 45000`，均使用总空闲堆，未使用 `ESP.getMaxAllocHeap()` 或 `heap_caps_get_largest_free_block()` |
| **建议重构动作** | 改为“总量 + 最大连续块”双守卫；阈值提取为可配置常量；失败路径打印 `getMaxAllocHeap()` |
| **负责层** | App 层 |

### A2: 内存阈值硬编码，缺乏场景自适应

| 项目 | 内容 |
|------|------|
| **ID** | A2 |
| **优先级** | P1 |
| **模块** | WiFi/BT 管理器 |
| **文件/行** | `src/app/bluetooth/bt_manager.cpp:106`；`src/app/wifi/wifi_manager.cpp:224` |
| **问题描述** | `BT_MIN_HEAP = 70000`、`WIFI_HEAP_THRESHOLD = 45000` 硬编码 |
| **建议重构动作** | 引入 `wifi_mgr_needed_heap()` / `bt_mgr_needed_heap()` 估算函数；结合内核暴露的系统保留内存接口 |
| **负责层** | App 层 + 内核支持 |

### A3: BT 启用时固定 delay 500ms，未轮询确认内存释放

| 项目 | 内容 |
|------|------|
| **ID** | A3 |
| **优先级** | P1 |
| **模块** | BT 管理器 |
| **文件/行** | `src/app/bluetooth/bt_manager.cpp:124-128` |
| **问题描述** | 关闭 WiFi 后固定 `delay(500)`，不检查堆是否回升到阈值 |
| **建议重构动作** | 改为轮询重试：最多 1500ms 内每 50ms 检查 `getFreeHeap()` 与 `getMaxAllocHeap()` 是否达标 |
| **负责层** | App 层 |

### A4: BT 初始化失败未区分错误类型

| 项目 | 内容 |
|------|------|
| **ID** | A4 |
| **优先级** | P0 |
| **模块** | BT UART 服务 / BT 管理器 |
| **文件/行** | `src/app/bluetooth/bt_uart_service.cpp:217-280`；`src/app/bluetooth/bt_manager.cpp:130-137` |
| **问题描述** | `bt_uart_service_init()` 仅返回 bool，失败统一按内存不足处理 |
| **建议重构动作** | 增加错误码（OK / ERR_HEAP / ERR_BLUEDROID / ERR_RADIO）；失败时记录 `free_heap`、`max_alloc_heap`、`g_wifi_was_on` |
| **负责层** | App 层 |

### A5: 所有 Xeros 任务栈固定 4096，未按实际负载分配

| 项目 | 内容 |
|------|------|
| **ID** | A5 |
| **优先级** | P1 |
| **模块** | 任务创建 / UI 任务 |
| **文件/行** | `src/main.cpp:318-325`；`src/kernel/kern_port_freertos.c:259-267` |
| **问题描述** | UI/WiFi-mgr/BT-mgr 全部 `kern_spawn(..., 4096)`，一刀切 |
| **建议重构动作** | 修复 P0-01 后，为各任务引入不同栈大小常量；结合 `kern_task_stack_usage()` 给出推荐值 |
| **负责层** | App 层 + 内核支持 |

### A6: BT SPP 静态环缓与动态队列冗余

| 项目 | 内容 |
|------|------|
| **ID** | A6 |
| **优先级** | P1 |
| **模块** | BT UART 服务 |
| **文件/行** | `src/app/bluetooth/bt_uart_service.h:22-23`；`src/app/bluetooth/bt_uart_service.cpp:75-76`、`211-214` |
| **问题描述** | 512B+512B 静态环缓 + 512B 动态队列，TX 环缓硬件路径几乎未使用 |
| **建议重构动作** | 移除未使用 TX 环缓或懒分配；明确 RX 走队列或环缓之一 |
| **负责层** | App 层 |

### A9: `g_wifi_on` / `g_bt_on` 状态可能与驱动实际状态脱节

| 项目 | 内容 |
|------|------|
| **ID** | A9 |
| **优先级** | P0 |
| **模块** | App 状态 / 设置 |
| **文件/行** | `src/app/app_state.c:12-13`；`src/app/app_init.c:40`；`src/app/bluetooth/bt_manager.cpp:244-248` |
| **问题描述** | switch_item 绑定意图变量，启用失败时 UI 仍显示开启 |
| **建议重构动作** | 启用失败时回写 `g_wifi_on`/`g_bt_on`；增加 `wifi_mgr_is_driver_on()` / `bt_mgr_is_driver_on()` |
| **负责层** | App 层 |

### A10: App 未使用内核 `/proc/meminfo`，内存视图不统一

| 项目 | 内容 |
|------|------|
| **ID** | A10 |
| **优先级** | P1 |
| **模块** | App / 内核交互 |
| **文件/行** | `src/kernel/kern_procfs.c:138-159`；`src/app/wifi/wifi_manager.cpp:224` |
| **问题描述** | 内核已提供 `/proc/meminfo`，App 仍直接调用 `ESP.getFreeHeap()` |
| **建议重构动作** | 封装 `xeros_mem_available()` 统一接口，优先读取 `/proc/meminfo` 或 `kern_kmem_get_stats()` |
| **负责层** | App 层 + 内核支持 |

## 5. 本轮重构排期建议

### 阶段 2.1：修复内存预留与诊断能力（1-2 周）

目标：消除 WiFi/BT 因单位错误/固定下限导致的过度预留，建立内存可观测性。

处理：P0-01、P0-03、P1-01、P1-03、A1、A3、A4、A9

验证：
1. `kern_spawn("test", entry, NULL, 1024)` 在 FreeRTOS 后端实际创建 1024 字节栈；
2. `/proc/tasks` 与 shell `ps` 显示字节总量与请求一致；
3. `kern_kmem_get_stats()` 可返回总分配/释放/最大空闲块；
4. BT/WiFi 启用使用总量+最大连续块双守卫；
5. 硬件上 WiFi/BT/UI/shell 同时启动后空闲堆显著增加。

### 阶段 2.2：实现栈/内存按需自动分配（2-3 周）

目标：实现任务栈自动增长/缩小，以及调度器对内存压力的响应。

处理：P0-02、P1-02、P1-04、A2、A5、A6、A10

验证：
1. Native 后端任务栈可在使用率持续 >75% 时自动扩容；
2. FreeRTOS 后端至少实现“按历史峰值创建”策略；
3. `kern_sched_tick` 在内存高压下触发调度类回调；
4. 长时间运行后任务栈高水位稳定，无持续增长；
5. 硬件上 WiFi/BT 可稳定共存。

## 6. Public API 变更影响

| API | 变更 | 影响 |
|-----|------|------|
| `kern_spawn(stack_min)` | 语义澄清：始终为字节 | App 层调用代码无需修改 |
| `kern_task_stack_usage()` | 行为修正：FreeRTOS 后端返回字节 | shell/procfs 显示值会变化 |
| `kern_kmem_get_stats()` | 新增 | shell 可调用，App 可选使用 |
| `kern_task_stack_recommend()` | 新增 | 可选，供 App 参考 |
| `kern_sched_class_t.memory_pressure` | 新增可选回调指针 | 自定义调度类需适配 |

**结论**：本轮重构可通过“修复语义 + 新增可选 API”完成，不需要破坏现有 App 层调用。

## 7. 需要进一步确认的实现细节

1. **FreeRTOS `usStackDepth` 单位**：已通过源码注释和 FreeRTOS 规范确认为“字”。修复后需硬件验证 `uxTaskGetStackHighWaterMark` 与 `ESP.getFreeHeap()` 变化一致。
2. **`KERN_PORT_STACK_MIN=4096` 字节还是字**：当前代码传入 4096 给 `xTaskCreate`（字 = 16 KB）。修复为字节语义后，4096 字节 = 1024 字，需要确认是否满足 ESP32 FreeRTOS 最小栈要求。
3. **XEROS_NATIVE_SCHED 是否参与本轮**：当前 `platformio.ini` 未启用该后端，本轮以 FreeRTOS 后端为重点，但修复需保持三种后端语义一致。
