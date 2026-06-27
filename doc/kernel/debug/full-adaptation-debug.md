# Xeros 内核全功能适配 Debug 记录

> **Parent:** [Xeros 内核文档](../index.md)

## Commit 清单

| Task | Commit | 说明 |
|------|--------|------|
| TCB 扩展 | 642ef4a | 增加 notify/stats 字段 |
| 任务通知 | 7ed6253 / 70976de | 实现 give/notify/take/wait bits；修复丢失唤醒竞争 |
| 软件定时器 | 7b91798 | 命令队列 + 守护任务 |
| 临界区 | c0496a3 | Xtensa PS.INTLEVEL 临界区抽象 |
| ISR 安全 IPC | 84a05c7 | 中断安全 give/set/notify |
| 流缓冲区 | 4983d82 | 字节流 + 消息缓冲区 |
| 任务控制 | e69863f | 挂起/恢复/优先级/延迟 |
| 运行时统计/看门狗/栈溢出 | 457fed1 | 统计、看门狗、栈 canary 检测 |
| IPC 回归测试 | 9c0f22c | native IPC 测试覆盖 |
| native 调度测试隔离 | ce0cb9e | yield 不覆盖 SLEEPING/SUSPENDED |
| PI 互斥锁恢复 | bdf4515 | 恢复目标改为 base_priority |
| 核心启动桩 | 6f00d66 | 隔离 xTaskCreatePinnedToCore |
| 移除 vTaskDelay | 881b80a | hal_delay_ms 走 kern_sleep_ms |
| FIFO 桶优化 | 3f60de5 | 优先级桶 O(1) 热路径 |
| 事件组高 8 位 | c507020 | 保留高 8 位供内核内部使用 |

## 关键问题与解决方案

1. **PI 互斥锁 `orig_priority` 过时**
   - 现象：低优先级持有者在获取锁后提升了 `base_priority`，解锁时仍恢复到旧的 `orig_priority`。
   - 解决：解锁路径直接恢复到 `m->owner->base_priority`，不再依赖快照。

2. **任务通知等待不醒**
   - 现象：`kern_task_notify_take` 调用 `kern_sleep_ms` 后，通知到达但任务仍睡到超时。
   - 解决：`kern_task_notify` 在设置 `RECEIVED` 时同步将 `SLEEPING` 任务置为 `READY`，并发送 IPI。

3. **`kern_yield` 覆盖 SLEEPING/SUSPENDED**
   - 现象：`kern_sleep_ms` 和 `kern_task_suspend` 设置的状态被 `kern_yield` 改回 READY。
   - 解决：`kern_yield` 仅在当前状态为 RUNNING 时才改为 READY。

4. **事件组高 8 位被用户覆盖**
   - 现象：未对用户传入 bits 做掩码，高 8 位可用于内核扩展。
   - 解决：在 `kern_event_set/clear/wait` 中应用 `KERN_EVENT_VALID_BITS`。

5. **调试 printf 引发测试崩溃**
   - 现象：在 `kern_pi_mutex_lock` 中加入临时 `printf` 后，后续测试出现段错误。
   - 解决：移除调试输出，并确认是临时代码引入的副作用。

6. **PI 回归测试任务泄漏到后续测试**
   - 现象： spawned 任务在测试返回后继续运行，破坏下一个测试状态。
   - 解决：测试末尾循环调度直到 `kern_task_get(low_pid/high_pid)` 都返回 NULL。

## 验证结果

- `pio test -e native`：588 test cases，2 skipped，586 succeeded。
- `pio run -e m5stick-c-native`：SUCCESS，RAM 20.2%，Flash 74.0%。
- `pio run -e m5stick-c`：SUCCESS，RAM 20.2%，Flash 73.2%。

## 未解决问题

- 完全绕过 ESP-IDF 的核心启动需要直接操作复位寄存器，风险高，V2 保留最小启动桩。

---

> **See Also:** [FreeRTOS 调度依赖移除记录](../freertos-removal.md)
