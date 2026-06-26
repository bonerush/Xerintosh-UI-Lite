# 中断边界处理设计

> **Parent:** [原生内核架构](xeros-native-kernel.md) | **Related:** [IPC](ipc-primitives.md) | [调度器](scheduler.md)

## 概述

本文档描述中断与内核之间的边界划分设计，遵循 FreeRTOS 的延迟中断处理（Deferred Interrupt Processing）设计哲学。

## 设计原则

1. **ISR 最小化**: ISR 只做最少量工作（清标志、发信号量），真正耗时的处理交给高优先级任务
2. **中断嵌套支持**: 配置 NVIC（ARM）/ 中断控制器允许高优先级中断抢占低优先级中断
3. **xHigherPriorityTaskWoken 机制**: ISR 中调用 `_from_isr` API 后，通过标志通知 ISR 末尾执行上下文切换

## 中断处理架构

```
中断发生
    │
    ▼
┌─────────────────────────────────────────────┐
│ ISR 上下文 (硬件自动保存部分寄存器)          │
│                                             │
│  1. 保存剩余寄存器到栈帧                     │
│  2. 清除中断标志                             │
│  3. 执行最少量工作:                          │
│     - 读取硬件状态                           │
│     - 发送信号量/设置事件 (give_from_isr)    │
│  4. 检查 woken 标志                          │
│  5. 如果 woken: 设置 need_resched = true     │
│  6. 恢复寄存器                               │
│  7. 返回                                     │
└─────────────────────────────────────────────┘
    │
    │ (如果 need_resched == true)
    ▼
┌─────────────────────────────────────────────┐
│ 调度器上下文 (任务上下文中执行)              │
│                                             │
│  kern_sched_tick()                          │
│  pick_next_ready()                          │
│  xeros_ctx_save/restore()                   │
└─────────────────────────────────────────────┘
```

## ISR 安全 API

### 命名约定

所有 ISR 安全 API 以 `_from_isr` 结尾：

```c
// 信号量
kern_err_t kern_bin_sem_give_from_isr(kern_bin_sem_t *sem, bool *woken);
kern_err_t kern_sem_give_from_isr(kern_sem_t *sem, bool *woken);

// 消息队列
kern_err_t kern_queue_send_from_isr(kern_queue_t *q, const void *item, bool *woken);
kern_err_t kern_queue_recv_from_isr(kern_queue_t *q, void *item, bool *woken);

// 事件组
kern_err_t kern_event_set_from_isr(kern_event_t *ev, uint32_t bits, bool *woken);
```

### 实现模式

所有 `_from_isr` 变体遵循相同模式：

```c
kern_err_t kern_xxx_from_isr(kern_xxx_t *obj, ..., bool *woken)
{
    // 注意：ISR 中不能使用 spinlock_lock (可能死锁)
    // 使用 spinlock_lock_irqsave 或直接使用原子操作

    spinlock_lock(&obj->lock);

    // 执行操作（与非 ISR 版本相同）
    // ...

    // 检查是否需要唤醒任务
    if (需要唤醒任务) {
        从等待队列取出最高优先级任务
        加入就绪队列
        *woken = true;  // 通知 ISR 需要上下文切换
    }

    spinlock_unlock(&obj->lock);
    return KERN_OK;
}
```

### 典型 ISR 使用模式

```c
// 示例：UART 接收中断
void IRAM_ATTR uart_rx_isr(void *arg)
{
    bool woken = false;

    // 1. 清除中断标志
    uart_clear_rx_intr(UART_NUM_0);

    // 2. 读取数据
    uint8_t data = uart_read_byte(UART_NUM_0);

    // 3. 发送到消息队列
    kern_queue_send_from_isr(&uart_rx_queue, &data, &woken);

    // 4. 如果唤醒了更高优先级任务，设置重调度标志
    if (woken) {
        g_need_resched = true;
    }

    // 5. ISR 返回（硬件恢复寄存器）
    // 如果 g_need_resched == true，调度器在下一个调度点执行上下文切换
}
```

## 中断嵌套支持

### ESP32 中断级别

```
Level 1: 最低优先级 (tick 定时器, 一般外设)
Level 2: 中等优先级 (UART, SPI)
Level 3: 较高优先级 (时间敏感外设)
Level 4-6: 高优先级 (必须用汇编处理)
Level 7: NMI (不可屏蔽中断)
```

### 嵌套规则

1. 高优先级中断可以抢占低优先级中断
2. 同级中断不能嵌套
3. 最高级别中断 (NMI) 中不能调用任何内核 API

### 中断级别管理

```c
// 保存当前中断级别并禁用中断
uint32_t irqs_save(void)
{
    uint32_t old_ps;
    __asm__ volatile("rsil %0, %1" : "=r"(old_ps) : "i"(XCHAL_EXCM_LEVEL));
    return old_ps;
}

// 恢复中断级别
void irqs_restore(uint32_t old_ps)
{
    __asm__ volatile("wsr.ps %0; rsync" :: "r"(old_ps));
}
```

## 延迟中断处理

### 设计模式

```
ISR (快速路径)              高优先级任务 (慢速路径)
┌───────────────┐          ┌───────────────────┐
│ 清除中断标志   │          │ 等待信号量/队列    │
│ 读取硬件状态   │ ────────→│ 处理数据           │
│ 发送信号量     │          │ 更新状态           │
│ 返回           │          │ 循环等待           │
└───────────────┘          └───────────────────┘
```

### 优势

1. **减少中断延迟**: ISR 执行时间从微秒级降到纳秒级
2. **避免优先级反转**: 耗时处理在任务上下文执行，可以被抢占
3. **简化错误处理**: 任务上下文可以使用完整的错误处理机制
4. **降低栈使用**: 不需要在 ISR 栈上分配大量局部变量

## xHigherPriorityTaskWoken 等价机制

### FreeRTOS 模式

```c
// FreeRTOS ISR 模式
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

### Xeros 等价模式

```c
// Xeros ISR 模式
bool woken = false;
kern_bin_sem_give_from_isr(sem, &woken);
if (woken) {
    g_need_resched = true;  // 在下一个调度点触发上下文切换
}
```

### 实现细节

```c
// 在 tick ISR 中检查
static void IRAM_ATTR tick_timer_isr(void *arg)
{
    // ... tick 处理 ...

    // 检查是否有任务被唤醒
    if (g_need_resched) {
        // 调度器在下一个 kern_sched_tick() 中执行上下文切换
        // 不在 ISR 中直接切换（避免栈溢出和复杂性）
    }
}
```

## ISR 栈管理

### 独立 ISR 栈

ESP32 使用独立的 ISR 栈，与任务栈分离：

```c
// ISR 栈配置
#define ISR_STACK_SIZE  2048  // 字

// 每个核心有独立的 ISR 栈
static uint8_t isr_stack_cpu0[ISR_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t isr_stack_cpu1[ISR_STACK_SIZE] __attribute__((aligned(16)));
```

### 栈溢出保护

```c
// ISR 栈金丝雀
#define ISR_STACK_CANARY  0xDEADC0DE

void check_isr_stack_overflow(void)
{
    uint32_t *canary = (uint32_t *)isr_stack_cpu0;
    if (*canary != ISR_STACK_CANARY) {
        // ISR 栈溢出！
        kern_panic("ISR stack overflow on CPU 0");
    }
}
```

## 测试策略

### ISR 延迟测试

```c
// 使用 GPIO 翻转测量 ISR 延迟
void test_isr_latency(void)
{
    GPIO_OUTPUT_SET(TEST_PIN, 1);  // 触发中断
    // ISR 中: GPIO_OUTPUT_SET(TEST_PIN, 0)
    // 测量脉冲宽度 = ISR 延迟
}
```

### 嵌套中断测试

```c
// 创建两个不同优先级的中断
// 高优先级中断抢占低优先级中断
// 验证嵌套正确执行
```

### 延迟处理测试

```c
// ISR 发送信号量
// 高优先级任务接收并处理
// 验证处理延迟 < 预期阈值
```

---

> **See Also:** [IPC](ipc-primitives.md) | [调度器](scheduler.md)
