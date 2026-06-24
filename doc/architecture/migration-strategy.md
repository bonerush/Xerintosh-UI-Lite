# 迁移策略

> **Parent:** [原生内核架构](xeros-native-kernel.md) | **Related:** [实施计划](../implementation-plan.md)

## 概述

本文档描述从 FreeRTOS 后端平滑迁移到 Xeros 原生后端的策略。迁移采用渐进式方法，确保系统在任何阶段都可编译和运行。

## 迁移原则

1. **向后兼容**: FreeRTOS 后端始终保留为备选
2. **编译时切换**: 通过 `XEROS_NATIVE_SCHED` 编译标志选择后端
3. **渐进式**: 每个阶段独立可测试
4. **零停机**: 迁移过程中系统始终可用

## 阶段 1: 并行开发

```
当前状态:
├── FreeRTOS 后端 (生产) ← 继续使用
└── 原生后端 (开发中) ← 新增，编译时可选

编译选项:
  pio run -e m5stick-c              # FreeRTOS 后端
  pio run -e m5stick-c-native       # 原生后端 (新增)
```

**关键点:**
- 两个后端共享相同的 `kern_port_ops_t` 接口
- 所有内核模块通过接口调用，不直接依赖后端
- 原生后端功能逐步完善，不影响 FreeRTOS 后端

## 阶段 2: 功能对等

```
验证标准:
- 原生后端实现所有 FreeRTOS 后端的功能
- 通过相同的集成测试套件
- 硬件上运行完整应用无异常
```

**检查清单:**
- [ ] 任务创建/销毁
- [ ] 任务调度 (RR + FIFO)
- [ ] 上下文切换 (yield, sleep, exit)
- [ ] 抢占式调度 (tick 中断)
- [ ] SMP 双核支持
- [ ] IPC 原语 (semaphore, mutex, queue, event)
- [ ] 内存管理
- [ ] Shell 命令
- [ ] VFS/devfs/procfs/sysfs

## 阶段 3: 性能优化

```
基准测试:
- 上下文切换延迟
- IPC 操作吞吐量
- 调度延迟
- 内存分配速度
- 功耗对比
```

**优化方向:**
- 上下文切换热路径 (IRAM 放置)
- 减少临界区长度
- 优化等待队列操作
- Tickless idle 节省功耗

## 阶段 4: 默认切换

```c
// platformio.ini 默认环境改为原生后端
[env]
default_envs = m5stick-c-native

// FreeRTOS 后端保留为备选
[env:m5stick-c-fallback]
; 保留原有配置
```

## FreeRTOS 依赖清单

### 必须移除的依赖

| 文件 | FreeRTOS API | 替代方案 |
|------|-------------|----------|
| `kern_port_freertos.c` | `xTaskCreatePinnedToCore` | 原生上下文创建 |
| `kern_port_freertos.c` | `xSemaphoreCreateBinary` | `kern_bin_sem_t` |
| `kern_port_freertos.c` | `xSemaphoreGive/Take` | `kern_bin_sem_give/take` |
| `kern_port_freertos.c` | `vTaskDelete` | 原生任务销毁 |
| `kern_smp.c` | `xPortGetCoreID` | 直接读寄存器 |
| `kern_smp.c` | `xTaskCreatePinnedToCore` | 原生核心启动 |
| `main.cpp` | `vTaskDelay` | `kern_sleep_ms` |
| `hal_system.cpp` | `vTaskDelay` | `kern_sleep_ms` |
| `ui_task.c` | `vTaskDelay` | `kern_sleep_ms` |
| `wifi_manager.cpp` | `vTaskDelay` | `kern_sleep_ms` |
| `dev_ttyS0.cpp` | `portMUX_TYPE` | 原生 spinlock |
| `kern_gpiofs.c` | `portMUX_TYPE` | 原生 spinlock |

### 可保留的依赖

| 依赖 | 原因 |
|------|------|
| ESP-IDF 框架 | 提供硬件抽象层 (GPIO, UART, SPI 等) |
| ESP-IDF multi_heap | 内存管理，不依赖 FreeRTOS |
| ESP-IDF 定时器 API | 硬件定时器控制 |
| ESP-IDF WiFi/BT | 系统服务，需要 FreeRTOS 任务支持 |

## WiFi/BT 兼容性

**关键问题:** ESP-IDF 的 WiFi 和蓝牙栈依赖 FreeRTOS 任务。完全移除 FreeRTOS 会导致这些服务不可用。

**解决方案:** 保持 FreeRTOS 编译但不使用其调度器。WiFi/BT 任务由 Xeros 通过 FreeRTOS 兼容层管理：

```c
// 兼容层：将 FreeRTOS 任务映射到 Xeros 任务
void freertos_compat_init(void)
{
    // 保持 FreeRTOS idle 任务运行（喂 WDT）
    // WiFi/BT 任务通过 Xeros 调度
}
```

## 编译时后端选择

```c
// kern_port.h
#ifdef XEROS_NATIVE_SCHED
    // 原生后端
    #include "kern_port_esp32_native.h"
    // g_kern_port_ops 由 kern_port_esp32_native.c 定义
#else
    // FreeRTOS 后端
    #include "kern_port_freertos.h"
    // g_kern_port_ops 由 kern_port_freertos.c 定义
#endif
```

## 回滚策略

如果原生后端出现问题，可以快速回滚到 FreeRTOS 后端：

1. 移除 `XEROS_NATIVE_SCHED` 编译标志
2. 重新编译
3. 刷写固件

**回滚时间:** < 5 分钟（编译 + 刷写）

## 迁移检查清单

### 迁移前
- [ ] 所有单元测试通过
- [ ] 硬件集成测试通过
- [ ] 性能基准记录
- [ ] 文档更新

### 迁移中
- [ ] 并行运行两个后端
- [ ] 监控系统稳定性
- [ ] 记录任何异常

### 迁移后
- [ ] 长时间稳定性测试 (24h)
- [ ] 功耗对比
- [ ] 用户验收测试
- [ ] 更新文档

---

> **See Also:** [原生内核架构](xeros-native-kernel.md) | [实施计划](../implementation-plan.md)
