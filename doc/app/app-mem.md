# App 层统一内存视图（App Mem）

> **Parent:** [App 层索引](index.md) | **Related:** [内核分配器](../kernel/kern-kmalloc.md), [WiFi 管理器](wifi.md), [蓝牙管理器](bluetooth.md), [全局状态](app-state.md)

## 概述

`app_mem.h/c` 为 App 层提供**统一内存视图**，把内核的 `kern_kmem_*` 统计/压力接口包装成 App 易用的安全分配判断函数。

设计动机：WiFi/BT 等重资源服务的启用不能只看 `ESP.getFreeHeap()` 的总空闲量，否则会因为：
- **最大连续空闲块不足**（堆碎片导致大对象分配失败）
- **未预留系统安全水位**（服务启动后普通任务无内存可用）

而造成启动成功但后续任务崩溃。App Mem 在总空闲、最大连续块、保留水位三个维度上做联合判断。

---

## 关键概念

### 内存统计快照

*📄 Source: [app_mem.h](../../src/app/app_mem.h#L26-L40)*

```c
bool xeros_mem_get_stats(kern_kmem_stat_t *out);
```

直接转发 `kern_kmem_get_stats()`，填充 `kern_kmem_stat_t`：

| 字段 | 含义 |
|------|------|
| `total_bytes` | 堆总字节数（ESP32）/ 0（native） |
| `free_bytes` | 当前空闲字节数 |
| `largest_free_block` | 当前最大连续空闲块 |
| `min_free_bytes` | 历史最小空闲字节数 |
| `allocated_bytes` | `kern_kmalloc` 已追踪分配字节总和 |
| `fragmentation_percent` | 估算碎片率 0-100 |

### 扣除保留水位后的可用内存

*📄 Source: [app_mem.c](../../src/app/app_mem.c#L17-L23)*

```c
uint32_t xeros_mem_available_bytes(void)
{
    kern_kmem_stat_t st;
    if (!kern_kmem_get_stats(&st)) return 0;
    if (st.free_bytes <= kern_kmem_reserved_bytes()) return 0;
    return (uint32_t)(st.free_bytes - kern_kmem_reserved_bytes());
}
```

返回 `free_bytes - reserved_bytes`，保证不会把系统预留的紧急水位算进去。

### 安全分配判断

*📄 Source: [app_mem.c](../../src/app/app_mem.c#L25-L43)*

```c
bool xeros_mem_can_alloc(uint32_t needed_bytes, uint32_t needed_contiguous)
```

判断逻辑：

| 后端 | 检查条件 |
|------|----------|
| **ESP32** | `free_bytes >= needed_bytes + reserved` **且** `largest_free_block >= needed_contiguous + reserved` |
| **native** | `allocated_bytes + needed_bytes <= budget`（默认 256KB 软预算） |

> **为什么同时检查总空闲和最大连续块？**  
> ESP32 的 Bluedroid/WiFi 驱动需要一次性 `malloc()` 大块连续内存。总空闲 100KB 但最大块只有 20KB 时，70KB 的 BT 初始化仍会失败。

---

## 使用模式

### 服务启用前内存守卫

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp#L115-L126)*

```c
#define BT_MIN_FREE_HEAP        70000
#define BT_MIN_MAX_ALLOC_HEAP   35000

if (!xeros_mem_can_alloc(BT_MIN_FREE_HEAP, BT_MIN_MAX_ALLOC_HEAP)) {
    kern_kmem_stat_t st;
    xeros_mem_get_stats(&st);
    Serial.printf("[BT] heap guard failed: free=%u max_alloc=%u reserved=%u\n",
                  (uint32_t)st.free_bytes,
                  (uint32_t)st.largest_free_block,
                  (uint32_t)kern_kmem_reserved_bytes());
    return BT_MGR_ERR_NO_MEM;
}
```

### 失败回写全局状态

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp)*

WiFi/BT 管理器在内存不足时返回错误码，调用方负责把 `g_wifi_on` / `g_bt_on` 回写为 `false`，避免 UI 开关与实际状态不一致。

```c
if (wifi_mgr_enable() != WIFI_MGR_OK) {
    g_wifi_on = false;   /* 同步全局状态 */
}
```

### 设置系统保留水位（可选）

*📄 Source: [kern_kmalloc.h](../../src/kernel/kern_kmalloc.h#L117-L122)*

```c
kern_kmem_set_reserved_bytes(32 * 1024);  /* 预留 32KB 系统安全水位 */
```

保留水位在 `xeros_mem_available_bytes()` 和 `xeros_mem_can_alloc()` 中自动扣除。当前启动流程未设置固定保留值（默认 0），后续可根据真机测试调整。

---

## 与内核层的关系

```
App 层
  │  xeros_mem_can_alloc(needed_bytes, needed_contiguous)
  ▼
app_mem.c
  │  调用 kern_kmem_get_stats() / kern_kmem_reserved_bytes()
  ▼
kern_kmalloc.c
  │  读取 ESP-IDF heap_caps_*() / 内部 allocated_bytes 统计
  ▼
ESP32 堆 / libc malloc
```

- **不替代 `kern_kmalloc`**：普通小对象分配仍走 `kern_kmalloc()` 自动追踪。
- **不处理外部 malloc**：`xeros_mem_can_alloc()` 统计的是整个堆，因此即使代码里混用 `malloc()` 也会反映在 `free_bytes` 中。

---

## 测试

*📄 Source: [test_app_mem.cpp](../../test/test_native/test_app_mem.cpp)*

| 用例 | 说明 |
|------|------|
| `AppMemTest.MemGetStatsWrapsKernel` | 包装内核接口返回有效统计 |
| `AppMemTest.MemAvailableZeroWhenReservedExceedsFree` | 保留水位高于 free 时返回 0 |
| `AppMemTest.MemCanAllocNativeBudget` | native 预算内可分配 |
| `AppMemTest.MemCanAllocFailsOverBudget` | 超预算返回 false |

---

> **See Also:** [内核分配器](../kernel/kern-kmalloc.md) | [WiFi 管理器](wifi.md) | [蓝牙管理器](bluetooth.md)
