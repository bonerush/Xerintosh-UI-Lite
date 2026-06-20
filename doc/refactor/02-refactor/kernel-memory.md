# 阶段 2.1 重构计划：kernel-memory（内存预留与诊断能力）

> **Scope**: P0-01、P0-03、P1-01、P1-03、A1、A3、A4、A9  
> **目标**: 消除 WiFi/BT 因 FreeRTOS 任务栈单位错误/固定下限导致的过度预留，建立内存可观测性。  
> **基线**: `doc/refactor/00-baseline-memory-schedule.md`  
> **诊断**: `doc/refactor/01-diagnosis-memory-schedule.md`

---

## 1. 阶段目标与验收标准

### 1.1 核心目标

1. **统一任务栈公共语义**：`kern_spawn(name, entry, arg, stack_min)` 的 `stack_min` 参数在所有后端（NATIVE_TEST / XEROS_NATIVE_SCHED / FreeRTOS）中均表示**字节**。
2. **修复 FreeRTOS 4 倍过度预留**：FreeRTOS 后端在调用 `xTaskCreatePinnedToCore` 前将字节转换为 `sizeof(StackType_t)` 字数。
3. **统一 TCB `stack_size` 字段**：所有后端 `task->stack_size` 均按**字节**存储。
4. **统一栈使用率显示**：`kern_task_stack_usage()`、`/proc/tasks`、shell `ps` 命令均按字节显示，不再在 FreeRTOS 路径下 `*4`。
5. **增强内存可观测性**：新增 `kern_kmem_stat_t` 与 `kern_kmem_get_stats()`，在 `/proc/meminfo` 与 shell `free/meminfo` 中暴露总空闲、最大连续块、历史最小空闲等。
6. **扩大资源节点池**：将 `kern_resource.c` 的固定池 `RES_POOL_SIZE=32` 改为与 `KERN_MAX_TASKS` 关联。
7. **补齐 XEROS_NATIVE_SCHED 栈监控**：提取后端无关的 `sched_check_stack_pressure()`，三种后端 tick 中统一调用。
8. **修复 App 层内存守卫与状态同步**：
   - BT/WiFi 启用前同时检查总堆与最大连续块。
   - BT 启用前关闭 WiFi 后的等待改为轮询，不再固定 `delay(500)`。
   - BT UART 初始化返回具体错误码（HEAP / BLUEDROID / RADIO / UNKNOWN）。
   - 启用失败时回写 `g_wifi_on` / `g_bt_on`，新增 `wifi_mgr_is_driver_on()` / `bt_mgr_is_driver_on()`。

### 1.2 验收标准（Acceptance Criteria）

| ID | 验收项 | 验证方式 |
|---|---|---|
| AC-01 | `kern_spawn("test", entry, NULL, 1024)` 在 FreeRTOS 后端实际创建 1024 字节（256 字）栈 | 硬件日志 + `ps` 输出 |
| AC-02 | `/proc/tasks` 与 `ps` 的 STACK 列显示 `已用字节/总字节`，且总量等于请求值 | shell / procfs |
| AC-03 | `kern_kmem_get_stats()` 返回 `free_bytes`、`largest_free_block`、`min_free_bytes` | native 单元测试 + 硬件 `cat /proc/meminfo` |
| AC-04 | `kern_resource_track()` 在 16 任务场景下不耗尽对象池 | `test_kernel_resource.cpp` |
| AC-05 | XEROS_NATIVE_SCHED 后端 tick 中同样触发 >75% 栈使用率告警 | 代码审查 + 构建 |
| AC-06 | WiFi 启用失败时 `g_wifi_on` 被回写为 `false`；BT 启用失败时 `g_bt_on` 被回写为 `false` | 单元测试 + 硬件 |
| AC-07 | BT 启用前对 `ESP.getMaxAllocHeap()` 做检查，且 WiFi 关闭后最多轮询 1500ms 等待内存回升 | 代码审查 + 硬件 |
| AC-08 | `pio test -e native` 不引入新的失败用例 | CI / 本地 |
| AC-09 | 硬件上 WiFi/BT/UI/shell 同时启动后，空闲堆显著高于基线 | 硬件串口日志 |

---

## 2. 具体实现步骤（按文件列出修改点）

### 2.1 内核层：栈单位语义统一（P0-01）

#### 2.1.1 `src/kernel/kern_port.h`

- 更新 `kern_port_thread_spawn` 的文档注释：
  > `stack_size` 栈大小（**字节**，0 表示使用默认值）
- 更新 `kern_port_thread_stack_usage` 的文档注释：
  > 返回**剩余栈字数**（FreeRTOS 后端）或**已使用栈字节数**（Native 后端）。调用者 `kern_task_stack_usage()` 负责按后端统一转换为字节。

> **说明**：保持 `kern_port_ops_t.thread_stack_usage` 签名不变，避免所有后端和调用点一起改动。语义差异由 `kern_task_stack_usage()` 屏蔽。

#### 2.1.2 `src/kernel/kern_port_freertos.c`

修改 `kern_port_freertos_thread_spawn()`：

```c
static kern_port_thread_t kern_port_freertos_thread_spawn(
    void (*entry)(void *arg), void *arg, const char *name,
    size_t stack_size, kern_task_t *task)
{
    (void)entry; (void)arg;

    if (task == NULL) return KERN_PORT_THREAD_NULL;

    if (stack_size < KERN_PORT_STACK_MIN) {
        stack_size = KERN_PORT_STACK_MIN;
    }

    /* stack_size 为字节，转换为 FreeRTOS 需要的字数（向上对齐） */
    size_t stack_words = stack_size / sizeof(StackType_t);
    if (stack_size % sizeof(StackType_t) != 0) {
        stack_words++;
    }

    /* 约束最小字数，避免低于 FreeRTOS 绝对下限 */
    if (stack_words < 1) stack_words = 1;

    TaskHandle_t handle = NULL;
    uint8_t cpu = task->cpu_id;
    if (cpu >= KERN_MAX_CPUS) cpu = 0;

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_wrapper,
        name ? name : "xtask",
        (uint32_t)stack_words,   /* 栈大小（字） */
        task,
        tskIDLE_PRIORITY + 1,
        &handle,
        cpu
    );

    if (ret != pdPASS) {
        kern_log(KERN_LOG_WARN, "port: xTaskCreate failed for %s", name);
        return KERN_PORT_THREAD_NULL;
    }

    return (kern_port_thread_t)handle;
}
```

#### 2.1.3 `src/kernel/kern_task_lifecycle.c`

FreeRTOS 路径（`#else` 分支）：

```c
size_t stack_sz = (stack_min > 0) ? stack_min : (size_t)KERN_PORT_STACK_MIN;
if (stack_sz < KERN_PORT_STACK_MIN) stack_sz = KERN_PORT_STACK_MIN;

task->stack_size = stack_sz;   /* 明确按字节记录 */

task->port_thread = kern_port_thread_spawn(
    NULL, task, name ? name : "xtask", stack_sz, task);
```

Native / XEROS_NATIVE_SCHED 路径无需修改（已是字节语义）。

#### 2.1.4 `src/kernel/kern_task_stack.c`

修改 FreeRTOS 路径的 `kern_task_stack_usage()`：

```c
#else /* ESP32: FreeRTOS 管理栈 */

size_t kern_task_stack_usage(kern_task_t *task)
{
    if (task == NULL) return 0;

    if (task->port_thread != KERN_PORT_THREAD_NULL) {
        /* port 层返回剩余栈字数 */
        size_t free_words = kern_port_thread_stack_usage(task->port_thread);
        size_t free_bytes = free_words * sizeof(StackType_t);

        if (free_bytes >= task->stack_size) return 0;
        return task->stack_size - free_bytes;
    }
    return 0;
}

#endif
```

> **影响 native 测试**：无。native 测试走 `#ifdef NATIVE_TEST` 分支，返回字节逻辑不变。

#### 2.1.5 `src/kernel/kern_shell_cmds.c`

修改 `cmd_ps()` 的 FreeRTOS 分支：

```c
#else
    if (task->port_thread == KERN_PORT_THREAD_NULL) {
        kern_shell_println(tty, "shrd");
    } else {
        size_t used = kern_task_stack_usage(task);
        size_t total = task->stack_size;  /* 不再 *4 */
        char stack_buf[32];
        snprintf(stack_buf, sizeof(stack_buf), "%zu/%zu", used, total);
        kern_shell_println(tty, stack_buf);
    }
#endif
```

#### 2.1.6 `src/kernel/kern_procfs.c`

`procfs_tasks_generate()` 已经使用 `kern_task_stack_usage(t)` 和 `t->stack_size`，由于 `t->stack_size` 已统一为字节，**无需修改**。

---

### 2.2 内核层：kmalloc 内存统计（P0-03）

#### 2.2.1 `src/kernel/kern_kmalloc.h`

在 `#ifdef __cplusplus extern "C" { #endif` 区域内新增：

```c
/* ═══ 内存统计结构体 ═══ */

/**
 * @brief 内核内存统计快照
 * @note  部分字段在 NATIVE_TEST 后端为近似值或 0（不可用）。
 */
typedef struct {
    size_t total_bytes;           /* 堆总字节数（FreeRTOS）/ 0（native） */
    size_t free_bytes;            /* 当前空闲字节数 */
    size_t largest_free_block;    /* 最大连续空闲块 */
    size_t min_free_bytes;        /* 历史最小空闲字节数 */
    size_t allocated_bytes;       /* kmalloc 已追踪分配字节总和 */
    size_t fragmentation_percent; /* 碎片率估算 0-100（(1 - largest/free)*100） */
} kern_kmem_stat_t;

/**
 * @brief  获取当前内存统计
 * @param  out 输出结构体指针
 * @return true 成功填充，false 参数无效
 */
bool kern_kmem_get_stats(kern_kmem_stat_t *out);
```

#### 2.2.2 `src/kernel/kern_kmalloc.c`

新增全局统计变量：

```c
static size_t g_kmem_allocated_bytes = 0;
```

在 `kern_kmalloc_impl()` 的 `track=true` 分支成功后：

```c
g_kmem_allocated_bytes += size;
```

在 `kern_kfree()` 和 `kern_krealloc()` 的释放路径中：

```c
if (hdr->size <= g_kmem_allocated_bytes) {
    g_kmem_allocated_bytes -= hdr->size;
} else {
    g_kmem_allocated_bytes = 0;
}
```

实现 `kern_kmem_get_stats()`：

```c
bool kern_kmem_get_stats(kern_kmem_stat_t *out)
{
    if (out == NULL) return false;

    memset(out, 0, sizeof(*out));
    out->allocated_bytes = g_kmem_allocated_bytes;

#ifndef NATIVE_TEST
    out->total_bytes = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    out->free_bytes  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    out->min_free_bytes = esp_get_minimum_free_heap_size();

    if (out->free_bytes > 0 && out->largest_free_block <= out->free_bytes) {
        out->fragmentation_percent =
            100 - (out->largest_free_block * 100 / out->free_bytes);
    } else {
        out->fragmentation_percent = 0;
    }
#else
    /* native 环境无统一堆接口，只返回 kmalloc 自身统计 */
    out->free_bytes = 0;
    out->largest_free_block = 0;
    out->min_free_bytes = 0;
    out->fragmentation_percent = 0;
#endif

    return true;
}
```

#### 2.2.3 `src/kernel/kern_procfs.c`

修改 `procfs_meminfo_generate()`：

```c
static size_t procfs_meminfo_generate(char *content, size_t max_len)
{
    kern_kmem_stat_t st;
    kern_kmem_get_stats(&st);

    int written;
#ifndef NATIVE_TEST
    written = snprintf(content, max_len,
                       "MemTotal:    %" PRIu32 " kB\n"
                       "MemFree:     %" PRIu32 " kB\n"
                       "MemUsed:     %" PRIu32 " kB\n"
                       "LargestBlock:%" PRIu32 " kB\n"
                       "MinFree:     %" PRIu32 " kB\n"
                       "Fragmentation: %" PRIu32 "%%\n",
                       (uint32_t)(st.total_bytes / 1024),
                       (uint32_t)(st.free_bytes / 1024),
                       (uint32_t)((st.total_bytes - st.free_bytes) / 1024),
                       (uint32_t)(st.largest_free_block / 1024),
                       (uint32_t)(st.min_free_bytes / 1024),
                       (uint32_t)st.fragmentation_percent);
#else
    written = snprintf(content, max_len,
                       "MemTotal: N/A (native)\n"
                       "Allocated: %" PRIu32 " bytes\n",
                       (uint32_t)st.allocated_bytes);
#endif
    return (written > 0) ? (size_t)written : 0;
}
```

#### 2.2.4 `src/kernel/kern_shell_cmds.c`

修改 `cmd_free()`：

```c
static void cmd_free(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size)
{
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    char line[96];
    kern_kmem_stat_t st;
    kern_kmem_get_stats(&st);

#ifndef NATIVE_TEST
    snprintf(line, sizeof(line),
             "free=%" PRIu32 " max_alloc=%" PRIu32 " min_free=%" PRIu32 " frag=%" PRIu32 "%%",
             (uint32_t)st.free_bytes,
             (uint32_t)st.largest_free_block,
             (uint32_t)st.min_free_bytes,
             (uint32_t)st.fragmentation_percent);
#else
    snprintf(line, sizeof(line),
             "allocated=%" PRIu32 " (native mode)",
             (uint32_t)st.allocated_bytes);
#endif
    kern_shell_println(tty, line);
}
```

修改 `cmd_meminfo()` 使用 `kern_kmem_get_stats()` 替代直接调用 `heap_caps_*`。

---

### 2.3 内核层：资源池扩容（P1-01）

#### 2.3.1 `src/kernel/kern_resource.c`

```c
#define RES_POOL_SIZE (KERN_MAX_TASKS * 4)   /* 16 * 4 = 64 */

static kern_resource_t g_res_pool[RES_POOL_SIZE];
static uint64_t g_res_pool_bitmap;            /* 64 位位图 */
```

更新 `res_pool_alloc()` / `res_pool_free()` / `res_is_pooled()` 使用 64 位。

> 当前 `KERN_MAX_TASKS=16`，`RES_POOL_SIZE=64`。若未来 `KERN_MAX_TASKS` 超过 16，需要改为按位图宽度动态计算或断言 `RES_POOL_SIZE <= 64`。

---

### 2.4 内核层：统一栈监控（P1-03）

#### 2.4.1 `src/kernel/kern_sched.c`

在文件顶部（全局状态区之后）新增：

```c
/* ═══ 栈压力检查（后端无关）═══ */

static void sched_check_stack_pressure(kern_task_t *task)
{
    if (task == NULL || task == g_idle_task) return;
    if ((g_sched_ticks % 500) != 0) return;

    size_t usage = kern_task_stack_usage(task);
    if (usage > 0 && task->stack_size > 0
        && usage > task->stack_size * 3 / 4) {
        kern_log(KERN_LOG_WARN,
                 "task %s stack usage %zu/%zu (>75%%)",
                 task->name, usage, task->stack_size);
    }
}
```

在 `kern_sched_tick()` 的三个后端分支中，在调用完所有 `class->tick()` 之后、重调度之前加入：

```c
sched_check_stack_pressure(g_current_task);
```

> 注意 FreeRTOS 后端 `g_idle_task` 是 `g_per_cpu[cpu].idle_task` 的宏还是全局变量。先假设 `g_idle_task` 宏正确指向当前 CPU idle；如编译报错再调整。

---

### 2.5 App 层：BT/WiFi 内存守卫与状态同步

#### 2.5.1 `src/app/bluetooth/bt_uart_service.h`

新增错误码和返回值：

```c
/* ═══ 错误码 ═══ */

typedef enum {
    BT_UART_OK = 0,
    BT_UART_ERR_HEAP,        /* 内存不足 */
    BT_UART_ERR_BLUEDROID,   /* Bluedroid 初始化/状态错误 */
    BT_UART_ERR_RADIO,       /* BT controller 射频/状态错误 */
    BT_UART_ERR_UNKNOWN,     /* 其他未知错误 */
} bt_uart_err_t;

/**
 * @brief  初始化 Bluetooth UART 服务
 * @return BT_UART_OK 成功，其他为错误码
 */
bt_uart_err_t bt_uart_service_init(void);
```

#### 2.5.2 `src/app/bluetooth/bt_uart_service.cpp`

修改返回类型并细化错误判断：

```c
#define BT_UART_MIN_FREE_HEAP       50000
#define BT_UART_MIN_MAX_ALLOC_HEAP  25000

bt_uart_err_t bt_uart_service_init(void)
{
    if (g_initialized) {
        Serial.println("[BT] bt_uart_service_init: already initialized");
        return BT_UART_OK;
    }

    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected      = false;
    g_prev_connected = false;

    if (!g_rx_queue) {
        g_rx_queue = xQueueCreate(BT_RX_QUEUE_SIZE, sizeof(uint8_t));
    }
    if (!g_poll_done_sem) {
        g_poll_done_sem = xSemaphoreCreateBinary();
    }
    if (g_poll_done_sem) {
        xSemaphoreGive(g_poll_done_sem);
    }

    /* 内存守卫 */
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
    if (free_heap < BT_UART_MIN_FREE_HEAP || max_alloc < BT_UART_MIN_MAX_ALLOC_HEAP) {
        Serial.printf("[BT-INIT] ERR_HEAP free=%u max_alloc=%u\n", free_heap, max_alloc);
        return BT_UART_ERR_HEAP;
    }

    Serial.printf("[BT-INIT] begin() start free_heap=%u max_alloc=%u ms=%lu\n",
                  free_heap, max_alloc, (unsigned long)millis());
    Serial.flush();

    uint32_t t0 = millis();
    bool ok = g_bt_serial.begin("M5Stick-P1");
    uint32_t elapsed = millis() - t0;

    Serial.printf("[BT-INIT] begin()=%d took=%lums free_heap=%u max_alloc=%u\n",
                  (int)ok, (unsigned long)elapsed, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    Serial.flush();

    if (!ok) {
        Serial.println("[BT] BluetoothSerial.begin() failed");
        /* 尝试推断原因 */
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            return BT_UART_ERR_RADIO;
        }
        return BT_UART_ERR_BLUEDROID;
    }

    g_initialized = true;
    delay(500);
    Serial.printf("[BT-INIT] post-begin delay done, ready. free_heap=%u\n",
                  ESP.getFreeHeap());
    Serial.flush();

    return BT_UART_OK;
}
```

NATIVE_TEST 桩也同步改为返回 `bt_uart_err_t`：

```c
#ifdef NATIVE_TEST
bt_uart_err_t bt_uart_service_init(void)
{
    ringbuf_init(&g_tx_buf, BT_UART_TX_BUF_SIZE);
    ringbuf_init(&g_rx_buf, BT_UART_RX_BUF_SIZE);
    g_connected = false;
    g_rx_cb     = NULL;
    g_conn_cb   = NULL;
    return BT_UART_OK;
}
#endif
```

#### 2.5.3 `src/app/bluetooth/bt_manager.h`

新增：

```c
/* ═══ 错误码 ═══ */

typedef enum {
    BT_MGR_OK = 0,
    BT_MGR_ERR_HEAP,
    BT_MGR_ERR_BLUEDROID,
    BT_MGR_ERR_RADIO,
    BT_MGR_ERR_UNKNOWN,
} bt_mgr_err_t;

/**
 * @brief  查询蓝牙驱动是否真实初始化成功
 * @return true  BluetoothSerial 已 begin 成功
 */
bool bt_mgr_is_driver_on(void);

/**
 * @brief  返回 BT 启用建议的最小空闲内存（字节）
 */
uint32_t bt_mgr_needed_heap(void);
```

并修改 `bt_mgr_enable` 返回类型：

```c
bt_mgr_err_t bt_mgr_enable(void);
```

#### 2.5.4 `src/app/bluetooth/bt_manager.cpp`

新增常量与辅助函数：

```c
#define BT_MIN_FREE_HEAP        70000
#define BT_MIN_MAX_ALLOC_HEAP   35000
#define BT_WIFI_SHUTDOWN_TIMEOUT_MS 1500
#define BT_WIFI_SHUTDOWN_POLL_MS    50

bool bt_mgr_is_driver_on(void) { return g_bt_enabled; }

uint32_t bt_mgr_needed_heap(void) { return BT_MIN_FREE_HEAP; }
```

修改 `bt_mgr_enable()`：

```c
bt_mgr_err_t bt_mgr_enable(void) {
    if (g_bt_enabled) return BT_MGR_OK;

    /* 双守卫 */
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
    if (free_heap < BT_MIN_FREE_HEAP || max_alloc < BT_MIN_MAX_ALLOC_HEAP) {
        Serial.printf("[BT] heap guard failed: free=%u max_alloc=%u\n",
                      free_heap, max_alloc);
        g_bt_on = false;   /* A9: 回写状态 */
        return BT_MGR_ERR_HEAP;
    }

    g_wifi_was_on = wifi_mgr_is_enabled();
    if (g_wifi_was_on) {
        wifi_mgr_disable();
        /* 轮询等待 WiFi 内存释放，最多 1500ms */
        uint32_t wait_start = millis();
        while (millis() - wait_start < BT_WIFI_SHUTDOWN_TIMEOUT_MS) {
            if (ESP.getFreeHeap() >= BT_MIN_FREE_HEAP &&
                ESP.getMaxAllocHeap() >= BT_MIN_MAX_ALLOC_HEAP) {
                break;
            }
            delay(BT_WIFI_SHUTDOWN_POLL_MS);
        }
        if (ESP.getFreeHeap() < BT_MIN_FREE_HEAP ||
            ESP.getMaxAllocHeap() < BT_MIN_MAX_ALLOC_HEAP) {
            Serial.printf("[BT] WiFi shutdown did not free enough heap\n");
            /* 恢复 WiFi，保持用户意图 */
            wifi_mgr_enable();
            g_bt_on = false;   /* A9 */
            return BT_MGR_ERR_HEAP;
        }
    }

    bt_uart_err_t uerr = bt_uart_service_init();
    if (uerr != BT_UART_OK) {
        Serial.printf("[BT] bt_uart_service_init failed: %d\n", (int)uerr);
        if (g_wifi_was_on) {
            wifi_mgr_enable();
        }
        g_bt_on = false;   /* A9 */

        switch (uerr) {
        case BT_UART_ERR_HEAP:       return BT_MGR_ERR_HEAP;
        case BT_UART_ERR_RADIO:      return BT_MGR_ERR_RADIO;
        case BT_UART_ERR_BLUEDROID:  return BT_MGR_ERR_BLUEDROID;
        default:                     return BT_MGR_ERR_UNKNOWN;
        }
    }

    g_bt_enabled = true;
    g_state = BT_MGR_ENABLED;
    return BT_MGR_OK;
}
```

NATIVE_TEST 桩：

```c
bt_mgr_err_t bt_mgr_enable(void) { g_bt_enabled_test = true; return BT_MGR_OK; }
```

#### 2.5.5 `src/app/wifi/wifi_manager.h`

新增：

```c
/**
 * @brief  查询 WiFi 驱动是否真实初始化成功
 */
bool wifi_mgr_is_driver_on(void);

/**
 * @brief  返回 WiFi 启用建议的最小空闲内存（字节）
 */
uint32_t wifi_mgr_needed_heap(void);
```

#### 2.5.6 `src/app/wifi/wifi_manager.cpp`

新增常量与函数：

```c
#define WIFI_MIN_FREE_HEAP        45000
#define WIFI_MIN_MAX_ALLOC_HEAP   20000

bool wifi_mgr_is_driver_on(void) { return g_wifi_enabled; }

uint32_t wifi_mgr_needed_heap(void) { return WIFI_MIN_FREE_HEAP; }
```

修改 `wifi_mgr_enable()` 的内存守卫：

```c
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
    if (free_heap < WIFI_MIN_FREE_HEAP || max_alloc < WIFI_MIN_MAX_ALLOC_HEAP) {
        Serial.printf("[WiFi] heap guard failed: free=%u max_alloc=%u\n",
                      free_heap, max_alloc);
        wifi_popup_request("内存不足", 2000);
        g_wifi_enabled = false;
        g_wifi_on = false;   /* A9: 回写状态 */
        g_state = WIFI_MGR_IDLE;
        wifi_menu_rebuild_list(0);
        return;
    }
```

#### 2.5.7 `src/app/app_state.c` / `.h`

无需新增变量。`g_wifi_on` / `g_bt_on` 的回写在各自管理器中完成。

---

### 2.6 入口任务栈大小

#### 2.6.1 `src/main.cpp`

修复 P0-01 后，`kern_spawn(..., 4096)` 实际分配 4096 字节（原为 16384 字节），因此 UI/WiFi/BT 管理器任务栈大小**保持 4096 不变**即可释放约 36KB 堆内存。

如需进一步微调，可在阶段 2.2 结合 `kern_task_stack_usage()` 历史数据再决定，本阶段不改动。

---

## 3. 新增/修改的 API 签名与语义

### 3.1 内核层

```c
/* kern_kmalloc.h */
typedef struct {
    size_t total_bytes;
    size_t free_bytes;
    size_t largest_free_block;
    size_t min_free_bytes;
    size_t allocated_bytes;
    size_t fragmentation_percent;
} kern_kmem_stat_t;

bool kern_kmem_get_stats(kern_kmem_stat_t *out);
```

- 所有字段单位均为**字节**，`fragmentation_percent` 为 0-100。
- NATIVE_TEST 后端只填充 `allocated_bytes`，其余为 0。
- FreeRTOS 后端填充全部字段。

### 3.2 App 层

```c
/* bt_uart_service.h */
typedef enum {
    BT_UART_OK = 0,
    BT_UART_ERR_HEAP,
    BT_UART_ERR_BLUEDROID,
    BT_UART_ERR_RADIO,
    BT_UART_ERR_UNKNOWN,
} bt_uart_err_t;

bt_uart_err_t bt_uart_service_init(void);

/* bt_manager.h */
typedef enum {
    BT_MGR_OK = 0,
    BT_MGR_ERR_HEAP,
    BT_MGR_ERR_BLUEDROID,
    BT_MGR_ERR_RADIO,
    BT_MGR_ERR_UNKNOWN,
} bt_mgr_err_t;

bt_mgr_err_t bt_mgr_enable(void);
bool bt_mgr_is_driver_on(void);
uint32_t bt_mgr_needed_heap(void);

/* wifi_manager.h */
bool wifi_mgr_is_driver_on(void);
uint32_t wifi_mgr_needed_heap(void);
```

所有新增/修改的 API 均保持 C 兼容（头文件 `extern "C"` 内，使用 `typedef enum`、`uint32_t`、指针）。

---

## 4. 三种后端处理策略

| 后端 | 栈大小输入 | 栈分配 | `task->stack_size` | `kern_task_stack_usage()` | 栈监控 |
|---|---|---|---|---|---|
| **NATIVE_TEST** | 字节 | `kern_kmalloc_for_task()` 分配堆栈 | 字节 | 扫描 0xAA 得已用字节 | tick 中调用 `sched_check_stack_pressure()` |
| **XEROS_NATIVE_SCHED** | 字节 | 手动 `malloc()` 栈 + setjmp | 字节 | 扫描 0xAA 得已用字节 | **新增**：tick 中调用 `sched_check_stack_pressure()` |
| **FreeRTOS** | 字节 | `xTaskCreatePinnedToCore(stack_size / sizeof(StackType_t))` | 字节 | `task->stack_size - free_words * sizeof(StackType_t)` | **新增**：tick 中调用 `sched_check_stack_pressure()` |

> **关键不变量**：`kern_spawn(stack_min)` 在所有后端中均为字节；`task->stack_size` 在所有后端中均为字节；`kern_task_stack_usage()` 在所有后端中均返回字节。

---

## 5. 单元测试计划

### 5.1 新增测试用例

#### `test/test_native/test_kernel_stack.cpp`

- `StackSizeStoredAsBytes`：spawn 时传入 512，验证 `task->stack_size == 512`。
- `StackUsageReturnsByteValue`：验证返回值 `<= task->stack_size`。

#### `test/test_native/test_kernel_kmalloc.cpp`

- `MemoryStatsBasic`：分配若干内存后调用 `kern_kmem_get_stats()`，验证 `allocated_bytes` 增加。
- `MemoryStatsFragmentation`：连续分配/释放形成碎片后，验证 `fragmentation_percent` 在合理范围。
- `MemoryStatsNullOut`：传入 NULL 返回 false。

#### `test/test_native/test_kernel_resource.cpp`

- `PoolSizeLinkedToMaxTasks`：在一个任务上追踪超过 32 个资源（例如 40 个），全部成功且能正确释放。

#### `test/test_native/test_kernel_sched.cpp`

- `StackPressureWarningDoesNotCrash`：spawn 一个任务并触发多次 tick，验证不崩溃。
- `StackPressureDetectsHighUsage`：spawn 一个会写栈的任务，tick 后 `kern_task_stack_usage()` 增加（可选，视告警日志可测性）。

### 5.2 需要更新的现有测试

| 测试文件 | 影响 | 处理 |
|---|---|---|
| `test_kernel_stack.cpp` | 无破坏性影响 | 新增用例 |
| `test_kernel_kmalloc.cpp` | 无破坏性影响 | 新增用例 |
| `test_kernel_resource.cpp` | 无破坏性影响 | 新增用例 |
| `test_kernel_sched.cpp` | 无破坏性影响 | 新增用例 |
| `test_app_state.cpp` | 无破坏性影响 | 保持现有默认状态测试 |

### 5.3 不影响 native 测试的改动说明

- `kern_task_stack_usage()` 在 NATIVE_TEST 路径下返回字节，语义未变。
- `kern_port_thread_stack_usage()` 签名未变，NATIVE_TEST 桩未变。
- `cmd_ps` 的 NATIVE_TEST 分支未变。
- `procfs_tasks_generate` 未变。

### 5.4 测试执行命令

```bash
pio test -e native
```

或单独运行：

```bash
./.pio/build/native/program --gtest_filter=KernelStackTest.*:KernelKmallocTest.*:KernelResourceTest.*:KernelSchedTest.*
```

---

## 6. 硬件验证计划

### 6.1 构建与上传

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e m5stick-c --target upload
```

### 6.2 验证清单

1. **启动日志**：
   - 确认 `UI task spawned`、`WiFi manager spawned`、`BT manager spawned` 后空闲堆比基线高。
   - 示例期望：`free_heap` 在 `kern_spawn` 后比基线高约 36KB。

2. **Shell `ps`**：
   - 连接串口 `pio device monitor -e m5stick-c`。
   - 输入 `ps` / `tasks`。
   - 确认 STACK 列显示 `used/total`，且 total 等于 4096（UI/wifi-mgr/bt-mgr），不再显示 16384。

3. **`/proc/tasks`**：
   - 输入 `cat /proc/tasks`。
   - 确认栈总量为 4096 字节。

4. **`/proc/meminfo` 与 `meminfo` 命令**：
   - 输入 `cat /proc/meminfo` 与 `meminfo`。
   - 确认包含 `LargestBlock` 与 `Fragmentation`。

5. **WiFi 内存守卫**：
   - 人为降低堆（可通过多次 `malloc` 测试或观察 BT 开启后），尝试开启 WiFi。
   - 确认弹出“内存不足”且开关回写到关闭状态。

6. **BT 启用/禁用**：
   - 在 WiFi 开启状态下开启 BT。
   - 确认 WiFi 关闭后不再固定 delay 500ms，而是轮询等待。
   - 若内存不足，BT 开关回写为关闭。
   - BT 关闭后 WiFi 自动恢复。

7. **长时间稳定性**：
   - 运行 5 分钟，观察是否出现栈溢出或 TWDT 复位。
   - 周期性输入 `free` 观察 `min_free` 不持续下降。

---

## 7. 风险与回滚策略

### 7.1 主要风险

| 风险 | 说明 | 缓解措施 |
|---|---|---|
| **R1: FreeRTOS 最小栈不足** | 修复后 4096 字节 = 1024 字，可能低于某些任务实际需求（之前是 16384 字节） | 硬件验证时监控 `ps` 栈使用率；如出现 >75% 告警，临时增大该任务 `stack_min` 到 6144/8192 |
| **R2: 字数转换导致对齐问题** | 非 4 字节倍数请求会被向上取整 | 使用显式向上取整，避免截断 |
| **R3: `kern_kmem_get_stats()` 在 native 下信息不完整** | native 没有 `heap_caps_*` | 仅用于可观测性，不影响功能 |
| **R4: BT 错误码推断不准确** | `BluetoothSerial::begin()` 不暴露具体错误码 | 通过 `esp_bt_controller_get_status()` 辅助推断；不准确也比统一“失败”好 |
| **R5: 管理器状态回写破坏 UI 测试** | `g_bt_on` / `g_wifi_on` 是全局变量，native 测试有默认值测试 | 仅在硬件失败路径回写，NATIVE_TEST 桩不改动默认值逻辑 |

### 7.2 回滚策略

1. **代码回滚**：本轮改动分散在多个文件，建议分多次 commit：
   - commit 1: 内核层栈单位修复（P0-01）
   - commit 2: kmalloc 统计（P0-03）
   - commit 3: 资源池扩容（P1-01）
   - commit 4: 统一栈监控（P1-03）
   - commit 5: App 层内存守卫与状态同步（A1/A3/A4/A9）

2. **运行时回滚**：若硬件出现栈溢出，可快速在 `kern_port_freertos.c` 中临时将 `KERN_PORT_STACK_MIN` 提高，或在 `main.cpp` 中增大特定任务的 `stack_min`。

3. **功能开关**：新增的 `bt_mgr_is_driver_on()` / `wifi_mgr_is_driver_on()` 不影响现有代码，UI 可以选择性使用。

---

## 8. 文档同步

本轮涉及以下文档需要在代码合并后同步更新：

- `doc/kernel/kern-task.md`
  - 更新“动态栈管理”章节：明确 `task->stack_size` 单位为字节。
  - 更新 FreeRTOS 后端说明：`kern_task_stack_usage()` 通过 `uxTaskGetStackHighWaterMark()` 查询后转为字节。
  - 补充 XEROS_NATIVE_SCHED 栈监控说明。

- `doc/kernel/kern-port.md`
  - 更新 `kern_port_thread_spawn` 的 `stack_size` 参数说明为字节。
  - 更新三后端对比表中的栈管理行。

- `doc/kernel/kern-shell-cmds.md`
  - 更新 `ps` 命令示例：删除 `*4` 注释。
  - 更新 `free` / `meminfo` 命令说明。

- `doc/kernel/kern-types.md`（如存在）
  - 更新 `KERN_STACK_MIN/MAX/GROW` 为字节语义。

---

## 9. 实施顺序建议

1. **Step 1**: P0-01 内核栈单位修复（影响最大，先验证）
2. **Step 2**: P1-03 统一栈监控（依赖 Step 1 的 `kern_task_stack_usage()` 字节语义）
3. **Step 3**: P0-03 kmalloc 统计（独立，可与 Step 1/2 并行）
4. **Step 4**: P1-01 资源池扩容（独立，小改动）
5. **Step 5**: App 层 A1/A3/A4/A9（依赖 bt_uart_service 错误码）
6. **Step 6**: native 单元测试补充
7. **Step 7**: 硬件验证与文档同步

---

## 10. 实施记录

> 实施时间：2026-06-19/20
> 实施者：coder agent
> 工作区：`/Users/yukisala/subject/M5Stick-P1/.worktrees/refactor-2026-06-19-memory-schedule`

### 10.1 实际修改文件列表

| 类别 | 文件 | 说明 |
|------|------|------|
| 内核可移植层 | `src/kernel/kern_port.h` | 更新 `kern_port_thread_spawn` / `thread_stack_usage` 文档注释，明确字节/字数语义 |
| 内核可移植层 | `src/kernel/kern_port_freertos.c` | FreeRTOS 后端将输入字节转换为 `StackType_t` 字数后再调用 `xTaskCreatePinnedToCore` |
| 内核任务栈 | `src/kernel/kern_task_stack.c` | FreeRTOS 路径按 `stack_size`（字节）计算已用栈字节；补充 `#include <freertos/FreeRTOS.h>` |
| 内核 Shell | `src/kernel/kern_shell_cmds.c` | `ps` 不再 `*4`；`free` / `meminfo` 改用 `kern_kmem_get_stats()` |
| 内核 kmalloc | `src/kernel/kern_kmalloc.h` | 新增 `kern_kmem_stat_t` 与 `kern_kmem_get_stats()` 声明 |
| 内核 kmalloc | `src/kernel/kern_kmalloc.c` | 新增 `g_kmem_allocated_bytes` 统计；实现 `kern_kmem_get_stats()` |
| 内核 procfs | `src/kernel/kern_procfs.c` | `/proc/meminfo` 改用 `kern_kmem_get_stats()`，输出 LargestBlock / Fragmentation |
| 内核资源追踪 | `src/kernel/kern_resource.c` | `RES_POOL_SIZE` 改为 `KERN_MAX_TASKS * 4 = 64`，位图扩为 64 位 |
| 内核调度器 | `src/kernel/kern_sched.c` | 提取 `sched_check_stack_pressure()`，三种后端 tick 统一调用 |
| App BT UART | `src/app/bluetooth/bt_uart_service.h` | 新增 `bt_uart_err_t` 错误码；`bt_uart_service_init()` 返回错误码 |
| App BT UART | `src/app/bluetooth/bt_uart_service.cpp` | 硬件/NATIVE_TEST 实现均返回错误码；硬件路径增加内存守卫与错误推断 |
| App BT 管理器 | `src/app/bluetooth/bt_manager.h` | 新增 `bt_mgr_err_t` / `bt_mgr_is_driver_on()` / `bt_mgr_needed_heap()`；`bt_mgr_enable()` 返回错误码 |
| App BT 管理器 | `src/app/bluetooth/bt_manager.cpp` | 双内存守卫、WiFi 关闭后轮询等待、失败回写 `g_bt_on`、错误码映射 |
| App WiFi 管理器 | `src/app/wifi/wifi_manager.h` | 新增 `wifi_mgr_is_driver_on()` / `wifi_mgr_needed_heap()` |
| App WiFi 管理器 | `src/app/wifi/wifi_manager.cpp` | 双内存守卫、失败回写 `g_wifi_on`、`is_driver_on()` / `needed_heap()` 实现 |
| 测试桩/调用点 | `test/test_ble_uart/test_ble_uart.cpp` | `EXPECT_TRUE(bt_uart_service_init())` 改为 `EXPECT_EQ(..., BT_UART_OK)` |
| 单元测试 | `test/test_native/test_kernel_stack.cpp` | 新增 `StackSizeStoredAsBytes`、`StackUsageReturnsByteValue` |
| 单元测试 | `test/test_native/test_kernel_kmalloc.cpp` | 新增 `MemoryStatsBasic`、`MemoryStatsFragmentation`、`MemoryStatsNullOut` |
| 单元测试 | `test/test_native/test_kernel_resource.cpp` | 新增 `PoolSizeLinkedToMaxTasks`（40 个资源） |
| 单元测试 | `test/test_native/test_kernel_sched.cpp` | 新增 `StackPressureWarningDoesNotCrash` |

> 注：`doc/refactor/README.md` 显示为已修改，但这是工作区进入本轮重构时由上游/分支设置脚本更新的索引文件，不属于本次代码实施。

### 10.2 Commit 建议（按 Step 分组）

1. **commit 1 — P0-01 内核栈单位修复**
   - `src/kernel/kern_port.h`
   - `src/kernel/kern_port_freertos.c`
   - `src/kernel/kern_task_stack.c`
   - `src/kernel/kern_shell_cmds.c`

2. **commit 2 — P0-03 kmalloc 内存统计**
   - `src/kernel/kern_kmalloc.h`
   - `src/kernel/kern_kmalloc.c`
   - `src/kernel/kern_procfs.c`
   - `src/kernel/kern_shell_cmds.c`（free/meminfo 改动）

3. **commit 3 — P1-01 资源池扩容**
   - `src/kernel/kern_resource.c`

4. **commit 4 — P1-03 统一栈监控**
   - `src/kernel/kern_sched.c`

5. **commit 5 — A1/A3/A4/A9 App 层内存守卫与状态同步**
   - `src/app/bluetooth/bt_uart_service.h`
   - `src/app/bluetooth/bt_uart_service.cpp`
   - `src/app/bluetooth/bt_manager.h`
   - `src/app/bluetooth/bt_manager.cpp`
   - `src/app/wifi/wifi_manager.h`
   - `src/app/wifi/wifi_manager.cpp`

6. **commit 6 — native 单元测试补充与测试桩同步**
   - `test/test_ble_uart/test_ble_uart.cpp`
   - `test/test_native/test_kernel_stack.cpp`
   - `test/test_native/test_kernel_kmalloc.cpp`
   - `test/test_native/test_kernel_resource.cpp`
   - `test/test_native/test_kernel_sched.cpp`

### 10.3 验证结果

#### `pio test -e native`

```text
Environment    Test              Status    Duration
-------------  ----------------  --------  ------------
native         test_ble_uart     PASSED    00:00:01.232
native         test_native       ERRORED   00:00:01.184
native         test_token_usage  PASSED    00:00:00.956
================ 235 test cases: 234 succeeded in 00:00:03.372 ================
```

- **新增用例**：全部通过（`KernelStackTest.*`、`KernelKmallocTest.MemoryStats*`、`KernelResourceTest.PoolSizeLinkedToMaxTasks`、`KernelSchedTest.StackPressureWarningDoesNotCrash`）。
- **失败用例**：11 个，均为基线已存在的问题，未恶化：
  - `KernelVFSTest.MaxFdPerTask`
  - `KernelVFSTest.FdNamespaceIsolated`
  - `ShellCompleteTest.CompleteRelativeFromRoot` 等 7 个
  - `SpringAnimTest.OvershootsWithLowDamping`
- **崩溃/异常**：`test_native` 在全部用例执行后仍收到 `SIGHUP` 信号异常退出（基线为 `SIGTRAP`），未导致新的测试失败。

#### `pio run -e m5stick-c`

- **结果**：命令在 600 秒超时后仍未完成。
- **原因判断**：与基线报告一致，网络环境无法下载 `m5stack/M5Unified @ ^0.2.14`（SSL EOF / read timeout），属于环境/网络问题，非代码回归。
- **后续建议**：在可联网环境或缓存命中后重新执行硬件构建与上机验证。

### 10.4 与计划的偏差及替代方案

| 计划点 | 实际处理 | 原因 |
|--------|----------|------|
| `test_kernel_stack.cpp` 中 `StackSizeStoredAsBytes` 使用 512 字节 | 改用 **1024 字节**（`KERN_STACK_MIN`） | Native 后端 `task_stack_init()` 会将低于 `KERN_STACK_MIN` 的请求 clamp 到 1024，512 无法验证精确相等 |
| `kern_task_lifecycle.c` FreeRTOS 路径“需修改” | **未修改** | 当前代码已实现按字节选择 `stack_sz` 并记录 `task->stack_size = stack_sz`，与计划一致，无需额外改动 |
| 硬件构建验证 | **未能完成** | 网络超时，已记录为环境限制 |

### 10.5 遗留问题或风险

1. **R1 — FreeRTOS 最小栈实际占用**：修复后 4096 字节 = 1024 字。若某个任务真实栈需求超过此值，运行期可能触发 `sched_check_stack_pressure()` 的 >75% 告警。硬件验证时应通过 `ps` 监控，必要时将对应任务 `stack_min` 提升至 6144/8192 字节。
2. **R2 — BT 错误码推断准确性**：`BluetoothSerial::begin()` 不暴露具体错误码，当前通过 `esp_bt_controller_get_status()` 推断 RADIO / BLUEDROID，可能与真实原因有偏差，但不影响“失败时回写状态”的防御效果。
3. **R3 — `kern_kmem_get_stats()` native 信息不完整**：native 环境无 `heap_caps_*`，仅能统计 `allocated_bytes`，不影响功能，仅用于可观测性。
4. **R4 — 资源池位图宽度限制**：`RES_POOL_SIZE = KERN_MAX_TASKS * 4 = 64` 刚好占满 64 位位图。若未来增大 `KERN_MAX_TASKS`，需改为动态位图或数组。
5. **R5 — 硬件构建未验证**：本阶段涉及 FreeRTOS / ESP32 专用代码（栈字数转换、`ESP.getMaxAllocHeap()`、BT controller 状态等），必须在网络恢复后补做 `pio run -e m5stick-c` 与实际上机测试。
