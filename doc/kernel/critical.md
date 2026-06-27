# 临界区与中断抽象

> **Parent:** [Xeros 内核文档](index.md)

临界区接口屏蔽 Xtensa 处理器的中断，保证内核数据结构在 SMP/单核下的原子访问安全。

## 核心 API

*📄 Source: [kern_critical.h](../../src/kernel/kern_critical.h#L11-L16)*

```c
uint32_t kern_enter_critical(void);
void     kern_exit_critical(uint32_t state);

bool kern_interrupts_enabled(void);
void kern_disable_interrupts(void);
void kern_enable_interrupts(void);
```

## 关键实现

在 `XEROS_NATIVE_SCHED` 且非 native 测试环境下，通过读写 `PS` 寄存器的 `INTLEVEL` 字段提升中断级别到 `XCHAL_EXCM_LEVEL`。

*📄 Source: [kern_critical.c](../../src/kernel/kern_critical.c#L7-L23)*

```c
uint32_t kern_enter_critical(void)
{
    uint32_t ps;
    __asm__ volatile ("rsr %0, ps" : "=r"(ps));
    uint32_t old = ps & XCHAL_PS_INTLEVEL_MASK;
    ps = (ps & ~XCHAL_PS_INTLEVEL_MASK) | XCHAL_EXCM_LEVEL;
    __asm__ volatile ("wsr %0, ps" :: "r"(ps));
    return old;
}
```

Native 测试与 FreeRTOS 后端提供空桩，因为测试环境无需真实关中断。

*📄 Source: [kern_critical.c](../../src/kernel/kern_critical.c#L42-L49)*

```c
uint32_t kern_enter_critical(void) { return 0; }
void     kern_exit_critical(uint32_t state) { (void)state; }
...
```

## 使用示例

```c
uint32_t state = kern_enter_critical();
/* 访问全局共享数据 */
kern_exit_critical(state);
```

## 注意事项

- `kern_enter_critical` 返回旧中断级别，必须配对使用。
- 临界区内应尽量简短，避免长时间关闭中断。

---

> **See Also:** [IPC 原语](../architecture/ipc-primitives.md)
