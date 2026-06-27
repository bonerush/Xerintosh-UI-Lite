# 软件定时器

> **Parent:** [Xeros 内核文档](index.md)

软件定时器通过独立守护任务 `timerd` 处理命令队列与到期回调，避免在中断上下文执行用户回调。

## 核心 API

*📄 Source: [kern_timer.h](../../src/kernel/kern_timer.h#L29-L37)*

```c
kern_err_t kern_timer_init(void);
kern_err_t kern_timer_create(kern_timer_t *timer, const char *name,
                              kern_timer_callback_t cb, void *arg,
                              uint32_t period_ms, kern_timer_mode_t mode);
kern_err_t kern_timer_start(kern_timer_t *timer);
kern_err_t kern_timer_stop(kern_timer_t *timer);
kern_err_t kern_timer_reset(kern_timer_t *timer);
void       kern_timer_process(void);
void       kern_timer_reset_all(void);
```

模式：

*📄 Source: [kern_timer.h](../../src/kernel/kern_timer.h#L11-L14)*

```c
typedef enum {
    KERN_TIMER_ONCE = 0,
    KERN_TIMER_AUTORELOAD = 1
} kern_timer_mode_t;
```

## 关键实现

命令队列长度固定为 16，守护任务每 1ms 处理一次命令并检查到期定时器。

*📄 Source: [kern_timer.c](../../src/kernel/kern_timer.c#L22-L30)*

```c
#define KERN_TIMER_CMD_QUEUE_LEN 16
...
static kern_timer_cmd_t s_cmd_queue[KERN_TIMER_CMD_QUEUE_LEN];
static volatile uint8_t s_cmd_head = 0;
static volatile uint8_t s_cmd_tail = 0;
```

到期判断使用 `int32_t` 差值，兼容 32 位 tick 回绕。

*📄 Source: [kern_timer.c](../../src/kernel/kern_timer.c#L153-L187)*

```c
static void timer_process_expired(void)
{ ... }
```

## 使用示例

```c
static void led_toggle(void *arg) { ... }

kern_timer_t t;
kern_timer_create(&t, "led", led_toggle, NULL, 500, KERN_TIMER_AUTORELOAD);
kern_timer_start(&t);
```

## 注意事项

- 回调在守护任务上下文执行，可调用普通 IPC 但不能阻塞过长时间。
- 一次性定时器触发后自动从活动列表移除，避免重复触发。

---

> **See Also:** [任务控制](task-ctrl.md)
