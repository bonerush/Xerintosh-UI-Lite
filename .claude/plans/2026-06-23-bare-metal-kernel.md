# Xeros Bare-Metal Kernel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all FreeRTOS dependencies in Xeros kernel with bare-metal ESP32 (Xtensa LX6) implementation — real context switching, O(1) priority scheduler, per-CPU runqueues, and dual-core IPI support.

**Architecture:** Five-phase bottom-up refactor. Phase 1 builds context switching assembly. Phase 2 adds the scheduler. Phase 3 adds timers and sleep. Phase 4 migrates sync primitives and upper-layer APIs. Phase 5 brings up dual-core. Each phase produces compilable, testable code.

**Tech Stack:** ESP-IDF v5.x (hardware drivers retained), Xtensa LX6 assembly, C11, GCC `__sync_*` atomics, CCOMPARE0 timer, Xtensa software interrupts for IPI.

**Key constraints:**
- Maintain existing Xeros VFS / Device / HAL / App layers unchanged
- Preserve ESP-IDF hardware drivers (heap_caps, GPIO, NVS, etc.)
- `CMakeLists.txt` uses `GLOB_RECURSE` — any new `.c`/`.S` file under `src/` is auto-discovered
- Native test (`NATIVE_TEST`) build path must continue to work
- ESP32 WiFi stack deferred — FreeRTOS shim in Phase 5+

---

### Task 0: Preparation — Create branch and verify build

**Files:** None new

- [ ] **Step 1: Create feature branch**

```bash
cd /Users/yukisala/subject/Xerintosh && git checkout -b feat/bare-metal-kernel
```

- [ ] **Step 2: Record baseline build status**

```bash
cd /Users/yukisala/subject/Xerintosh && pio run -e m5stick-c 2>&1 | tail -5
```

Expected: Build should succeed (may have warnings). Record the result.

- [ ] **Step 3: Commit**

```bash
git commit --allow-empty -m "chore: start bare-metal kernel refactor branch"
```

---

## Phase 1: Context Switching Infrastructure

### Task 1.1: Create `kern_ctx.h` — context structure definition

**Files:**
- Create: `src/kernel/kern_ctx.h`

- [ ] **Step 1: Write the header file**

```c
/**
 * @file   kern_ctx.h
 * @brief  Xtensa LX6 bare-metal context structure
 * @details Defines kern_ctx_t for full register save/restore.
 *          Used by bare-metal scheduler (non-NATIVE_TEST, non-FreeRTOS path).
 */

#ifndef KERN_CTX_H
#define KERN_CTX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full Xtensa LX6 context (~80 bytes).
 * A1 (SP) is included in a[1] but also stored separately for debug. */
typedef struct kern_ctx {
    uint32_t pc;           /* program counter */
    uint32_t ps;           /* program status word */
    uint32_t a[16];        /* A0-A15 general purpose registers */
    uint32_t sar;          /* shift amount register */
    uint32_t lbeg;         /* loop begin address */
    uint32_t lend;         /* loop end address */
    uint32_t lcount;       /* loop count */
    uint32_t sp_shadow;    /* A1 shadow (debug convenience) */
} kern_ctx_t;

/* Assembly-implemented functions */

/** Save current CPU context into *from, restore context from *to.
 *  Does not return to caller — execution continues at to->pc. */
void kern_ctx_switch(kern_ctx_t *from, kern_ctx_t *to);

/** Initialize a context to start executing entry(arg) on the given stack.
 *  Sets PC=entry, SP=stack_top, A2=arg (Xtensa calling convention). */
void kern_ctx_init(kern_ctx_t *ctx, void (*entry)(void *arg),
                   void *arg, void *stack_top);

/** ISR-return context switch. Caller's context saved to *from, then
 *  *to is restored and execution continues there.
 *  Unlike kern_ctx_switch, this may be called from ISR context where
 *  some registers are already hardware-saved on the exception stack. */
void kern_ctx_switch_isr(kern_ctx_t *from, kern_ctx_t *to);

#ifdef __cplusplus
}
#endif

#endif /* KERN_CTX_H */
```

- [ ] **Step 2: Verify compilation (header-only, no .c yet)**

```bash
cd /Users/yukisala/subject/Xerintosh && pio run -e m5stick-c 2>&1 | tail -3
```

Expected: Should fail on undefined symbols (kern_ctx_switch etc.), which is correct — we haven't written the .S file yet. Alternatively, it may compile fine since nothing includes kern_ctx.h yet.

- [ ] **Step 3: Commit**

```bash
git add src/kernel/kern_ctx.h && git commit -m "feat(ctx): add Xtensa context structure header"
```

---

### Task 1.2: Create `kern_ctx_esp32.S` — Xtensa assembly context switch

**Files:**
- Create: `src/kernel/kern_ctx_esp32.S`

- [ ] **Step 1: Write the assembly implementation**

```asm
/**
 * @file   kern_ctx_esp32.S
 * @brief  Xtensa LX6 bare-metal context switch (assembly)
 * @details Full save/restore of all 16 AR registers + SAR + loop regs + PS + PC.
 *          Entry function: kern_ctx_switch(from, to)
 *          Calling convention: a2 = from, a3 = to (AR2=1st arg, AR3=2nd arg)
 */

#include <xtensa/corebits.h>

    .section .text, "ax"
    .align  4
    .global kern_ctx_switch
    .type   kern_ctx_switch, @function
    .global kern_ctx_init
    .type   kern_ctx_init, @function
    .global kern_ctx_switch_isr
    .type   kern_ctx_switch_isr, @function

/* ═══════════════════════════════════════════════════════════
 * kern_ctx_switch(kern_ctx_t *from, kern_ctx_t *to)
 *   a2 = from (context to save current state into)
 *   a3 = to   (context to restore state from)
 *
 * Saves ALL current registers into *from, then restores *to
 * and jumps to to->pc. Does NOT return to caller.
 * ═══════════════════════════════════════════════════════════ */
kern_ctx_switch:
    /* Save current PC (return address is in a0, but we want the caller's PC) */
    movi    a4, .L_switch_return
    s32i    a4, a2, 0           /* ctx->pc = resume point */

    /* Save PS */
    rsr     a4, PS
    s32i    a4, a2, 4           /* ctx->ps */

    /* Save A0-A15 (offset 8..71 in struct) */
    s32i    a0,  a2, 8          /* ctx->a[0] */
    s32i    a1,  a2, 12         /* ctx->a[1] (SP) */
    s32i    a2,  a2, 16         /* ctx->a[2] (will be overwritten by restore) */
    s32i    a3,  a2, 20         /* ctx->a[3] (will be overwritten by restore) */
    s32i    a4,  a2, 24         /* ctx->a[4] */
    s32i    a5,  a2, 28         /* ctx->a[5] */
    s32i    a6,  a2, 32         /* ctx->a[6] */
    s32i    a7,  a2, 36         /* ctx->a[7] */
    s32i    a8,  a2, 40         /* ctx->a[8] */
    s32i    a9,  a2, 44         /* ctx->a[9] */
    s32i    a10, a2, 48         /* ctx->a[10] */
    s32i    a11, a2, 52         /* ctx->a[11] */
    s32i    a12, a2, 56         /* ctx->a[12] */
    s32i    a13, a2, 60         /* ctx->a[13] */
    s32i    a14, a2, 64         /* ctx->a[14] */
    s32i    a15, a2, 68         /* ctx->a[15] */

    /* Save SAR */
    rsr     a4, SAR
    s32i    a4, a2, 72          /* ctx->sar */

    /* Save loop registers */
    rsr     a4, LBEG
    s32i    a4, a2, 76          /* ctx->lbeg */
    rsr     a4, LEND
    s32i    a4, a2, 80          /* ctx->lend */
    rsr     a4, LCOUNT
    s32i    a4, a2, 84          /* ctx->lcount */

    /* Save SP shadow (debug convenience) */
    s32i    a1, a2, 88          /* ctx->sp_shadow */

    /* ─── Now restore context from *to (a3) ─── */

    /* Restore PS */
    l32i    a4, a3, 4
    wsr     a4, PS
    rsync

    /* Restore SAR */
    l32i    a4, a3, 72
    wsr     a4, SAR

    /* Restore loop registers */
    l32i    a4, a3, 76
    wsr     a4, LBEG
    l32i    a4, a3, 80
    wsr     a4, LEND
    l32i    a4, a3, 84
    wsr     a4, LCOUNT

    /* Restore A2-A15 (skip A0/A1 for now) */
    l32i    a2,  a3, 16
    l32i    a4,  a3, 24
    l32i    a5,  a3, 28
    l32i    a6,  a3, 32
    l32i    a7,  a3, 36
    l32i    a8,  a3, 40
    l32i    a9,  a3, 44
    l32i    a10, a3, 48
    l32i    a11, a3, 52
    l32i    a12, a3, 56
    l32i    a13, a3, 60
    l32i    a14, a3, 64
    l32i    a15, a3, 68

    /* Load new SP into a4 temporarily, then restore A0 from new stack */
    l32i    a4, a3, 12         /* new SP (a[1]) */
    l32i    a0, a3, 8          /* new return address (a[0]) */

    /* Load new PC */
    l32i    a3, a3, 0          /* target PC */

    /* Switch stack */
    mov     a1, a4

    /* Jump to target PC */
    jx      a3

.L_switch_return:
    /* This label acts as the "return address" saved in from->pc.
     * When this context is later switched back TO, execution resumes here.
     * Since kern_ctx_switch does NOT return to its caller, we ret to
     * whatever was in a0 before. */
    ret


/* ═══════════════════════════════════════════════════════════
 * kern_ctx_init(kern_ctx_t *ctx, void (*entry)(void*), void *arg, void *stack_top)
 *   a2 = ctx       (context to initialize)
 *   a3 = entry     (function pointer)
 *   a4 = arg       (argument passed to entry)
 *   a5 = stack_top (initial stack pointer, grows downward)
 *
 * Initializes a context so that kern_ctx_switch(NULL, ctx) starts
 * executing entry(arg) on the given stack.
 * ═══════════════════════════════════════════════════════════ */
kern_ctx_init:
    /* Set initial PC = entry function */
    s32i    a3, a2, 0          /* ctx->pc = entry */

    /* Set initial PS: enable interrupts, ring 0, WOE=0, CALLINC=0 */
    movi    a7, (PS_INTLEVEL(0) | PS_UM | PS_WOE | PS_CALLINC(0))
    s32i    a7, a2, 4          /* ctx->ps */

    /* Set A0 = kern_exit trampoline return address (fallback) */
    movi    a7, _kern_ctx_exit_stub
    s32i    a7, a2, 8          /* ctx->a[0] = exit stub */

    /* Set SP = stack_top */
    s32i    a5, a2, 12         /* ctx->a[1] = SP */

    /* Set A2 = arg (first argument to entry) */
    s32i    a4, a2, 16         /* ctx->a[2] = arg */

    /* Zero all other registers */
    movi    a7, 0
    s32i    a7, a2, 20         /* a[3] */
    s32i    a7, a2, 24         /* a[4] */
    s32i    a7, a2, 28         /* a[5] */
    s32i    a7, a2, 32         /* a[6] */
    s32i    a7, a2, 36         /* a[7] */
    s32i    a7, a2, 40         /* a[8] */
    s32i    a7, a2, 44         /* a[9] */
    s32i    a7, a2, 48         /* a[10] */
    s32i    a7, a2, 52         /* a[11] */
    s32i    a7, a2, 56         /* a[12] */
    s32i    a7, a2, 60         /* a[13] */
    s32i    a7, a2, 64         /* a[14] */
    s32i    a7, a2, 68         /* a[15] */

    /* Zero SAR */
    s32i    a7, a2, 72         /* ctx->sar */

    /* Zero loop registers */
    s32i    a7, a2, 76         /* ctx->lbeg */
    s32i    a7, a2, 80         /* ctx->lend */
    s32i    a7, a2, 84         /* ctx->lcount */

    /* SP shadow */
    s32i    a5, a2, 88         /* ctx->sp_shadow */

    ret

/* Exit stub: called if a task returns from its entry function */
_kern_ctx_exit_stub:
    /* The stub simply loops forever — real exit is handled by kern_exit() */
1:  waiti   0
    j       1b


/* ═══════════════════════════════════════════════════════════
 * kern_ctx_switch_isr(kern_ctx_t *from, kern_ctx_t *to)
 *   a2 = from
 *   a3 = to
 *
 * Lightweight version for ISR context. On Xtensa, when an interrupt
 * fires, the hardware automatically saves AR0-AR3 and some state
 * onto the exception stack. This function saves the remaining
 * registers and restores the target context.
 *
 * For simplicity, this defers to kern_ctx_switch — the full save
 * cost (~40 cycles) is acceptable for our 1kHz tick ISR.
 * ═══════════════════════════════════════════════════════════ */
kern_ctx_switch_isr:
    /* Delegate to full switch (80 cycles vs 50 for optimized — acceptable) */
    j       kern_ctx_switch
```

- [ ] **Step 2: Update `src/kernel/kern_port.h`** to add bare-metal include path

In `src/kernel/kern_port.h`, find the platform selection block (around line 38-48):

```c
#ifdef NATIVE_TEST
  /* Native: use POSIX ucontext (no FreeRTOS) */
  #define KERN_PORT_STACK_MIN  1024
#elif defined(XEROS_NATIVE_SCHED)
  /* ESP32 native scheduler: setjmp/longjmp + manual stack management */
  #include "kern_ctx_esp32.h"
  #define KERN_PORT_STACK_MIN  4096
#else
  /* ESP32 + FreeRTOS task container (default) */
  #define KERN_PORT_STACK_MIN  4096
#endif
```

Replace the `#else` block with:

```c
#elif defined(XEROS_BARE_METAL)
  /* ESP32 bare-metal scheduler: no FreeRTOS, direct Xtensa context switch */
  #include "kern_ctx.h"
  #define KERN_PORT_STACK_MIN  4096
#else
  /* ESP32 + FreeRTOS task container (default — legacy) */
  #define KERN_PORT_STACK_MIN  4096
#endif
```

- [ ] **Step 3: Update `src/kernel/kern_task.h`** to add bare-metal ctx field

In `src/kernel/kern_task.h`, find the context save block (lines 59-65). After the existing three paths, add:

```c
    /* Context save */
#if defined(NATIVE_TEST)
    kern_ctx_t          ctx;            /* ucontext */
#elif defined(XEROS_NATIVE_SCHED)
    kern_ctx_t          ctx;            /* setjmp/longjmp */
#elif defined(XEROS_BARE_METAL)
    kern_ctx_t          ctx;            /* bare-metal full context */
#else
    kern_port_thread_t  port_thread;    /* FreeRTOS task handle */
#endif
```

- [ ] **Step 4: Commit**

```bash
git add src/kernel/kern_ctx_esp32.S src/kernel/kern_port.h src/kernel/kern_task.h
git commit -m "feat(ctx): add Xtensa assembly context switch (kern_ctx_switch, kern_ctx_init)

Implements full register save/restore for Xtensa LX6:
- All 16 AR registers (A0-A15)
- PS, SAR, loop registers (LBEG/LEND/LCOUNT)
- ~80 byte context structure
- Added XEROS_BARE_METAL compile path to kern_port.h and kern_task.h"
```

---

### Task 1.3: Create `kern_cpu.h` — per-CPU data structures and CPU ID

**Files:**
- Create: `src/kernel/kern_cpu.h`

- [ ] **Step 1: Write the header**

```c
/**
 * @file   kern_cpu.h
 * @brief  Per-CPU data structures and CPU identification for bare-metal kernel
 */

#ifndef KERN_CPU_H
#define KERN_CPU_H

#include "kern_types.h"
#include "kern_ctx.h"
#include "kern_runq.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-CPU state (one instance per core, stored in g_per_cpu[]) */
typedef struct kern_per_cpu {
    uint8_t      cpu_id;
    kern_task_t *current_task;       /* currently executing task */
    kern_task_t *idle_task;          /* idle task for this core */
    kern_runq_t  runq;               /* ready queue for this core */
    kern_ctx_t   idle_ctx;           /* saved idle task context */
    uint64_t     sched_ticks;        /* tick counter */
    volatile bool need_resched;      /* reschedule requested */
    volatile bool ipi_pending;       /* IPI received */
    uint8_t      task_count;         /* tasks assigned to this core */
    uint8_t      _pad[2];            /* alignment to 8 bytes */
} kern_per_cpu_t;

extern kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];

/* Per-CPU access macros */
#ifndef KERN_THIS_CPU
#define KERN_THIS_CPU   kern_cpu_id()
#endif

#ifndef g_current_task
#define g_current_task  (g_per_cpu[KERN_THIS_CPU].current_task)
#endif

#ifndef g_need_resched
#define g_need_resched  (g_per_cpu[KERN_THIS_CPU].need_resched)
#endif

/* Get current CPU core ID.
 * On ESP32: reads PRID (Processor ID) special register bit 13.
 * On native test: always returns 0. */
uint8_t kern_cpu_id(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_CPU_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_cpu.h && git commit -m "feat(cpu): add per-CPU data structures and access macros"
```

---

### Task 1.4: Create `kern_cpu.c` — CPU identification

**Files:**
- Create: `src/kernel/kern_cpu.c`

- [ ] **Step 1: Write the implementation**

```c
/**
 * @file   kern_cpu.c
 * @brief  CPU identification and per-CPU array definition
 */

#include "kern_cpu.h"

/* Per-CPU array */
kern_per_cpu_t g_per_cpu[KERN_MAX_CPUS];

#ifndef NATIVE_TEST

uint8_t kern_cpu_id(void)
{
    uint32_t prid;
    /* Xtensa PRID (Processor ID) register: bit 13 = core number (0=PRO, 1=APP) */
    __asm__ volatile("rsr.prid %0" : "=r"(prid));
    uint8_t id = (uint8_t)((prid >> 13) & 1);
    if (id >= KERN_MAX_CPUS) return 0;
    return id;
}

#else /* NATIVE_TEST */

uint8_t kern_cpu_id(void)
{
    return 0;
}

#endif
```

- [ ] **Step 2: Verify compilation**

```bash
cd /Users/yukisala/subject/Xerintosh && pio run -e m5stick-c 2>&1 | tail -5
```

Expected: Compiles successfully (new files are not yet used by any existing code, but they are auto-discovered by GLOB_RECURSE). There should be no errors for the new files.

- [ ] **Step 3: Commit**

```bash
git add src/kernel/kern_cpu.c && git commit -m "feat(cpu): add CPU ID detection via PRID register"
```

---

## Phase 2: Runqueue and Scheduler

### Task 2.1: Create `kern_runq.h` — runqueue interface

**Files:**
- Create: `src/kernel/kern_runq.h`

- [ ] **Step 1: Write the header**

```c
/**
 * @file   kern_runq.h
 * @brief  O(1) priority bitmap per-CPU ready queue
 */

#ifndef KERN_RUNQ_H
#define KERN_RUNQ_H

#include "kern_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Priority levels: 0=highest, 31=lowest (reserved for idle task) */
#define KERN_PRIO_MAX      32
#define KERN_PRIO_IDLE     31
#define KERN_PRIO_DEFAULT  16
#define KERN_TIME_SLICE_MS 10

/* Forward declaration */
struct kern_task;

/* Per-CPU ready queue with 32 priority levels and bitmap */
typedef struct kern_runq {
    struct kern_task *queues[KERN_PRIO_MAX];       /* head of each priority FIFO */
    struct kern_task *queue_tails[KERN_PRIO_MAX];  /* tail for O(1) append */
    uint32_t          bitmap;                      /* bit[i]=1 if prio i has tasks */
    uint8_t           count;                       /* total tasks in queue */
} kern_runq_t;

/* Enqueue a task into its priority queue. O(1). */
void kern_runq_enqueue(kern_runq_t *rq, struct kern_task *task);

/* Remove and return the highest-priority ready task. O(1).
 * Returns NULL if the queue is empty. */
struct kern_task *kern_runq_dequeue(kern_runq_t *rq);

/* Remove a specific task from the runqueue. O(n) in priority level.
 * Returns true if found and removed. */
bool kern_runq_remove(kern_runq_t *rq, struct kern_task *task);

/* Check if a specific task is in the runqueue. */
bool kern_runq_contains(kern_runq_t *rq, struct kern_task *task);

#ifdef __cplusplus
}
#endif

#endif /* KERN_RUNQ_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_runq.h && git commit -m "feat(runq): add O(1) priority bitmap runqueue header"
```

---

### Task 2.2: Create `kern_runq.c` — runqueue implementation

**Files:**
- Create: `src/kernel/kern_runq.c`

- [ ] **Step 1: Write the implementation**

```c
/**
 * @file   kern_runq.c
 * @brief  O(1) priority bitmap ready queue implementation
 */

#include "kern_runq.h"
#include "kern_task.h"

void kern_runq_enqueue(kern_runq_t *rq, kern_task_t *task)
{
    uint8_t prio = task->priority;
    if (prio >= KERN_PRIO_MAX) prio = KERN_PRIO_IDLE;

    rq->bitmap |= (1u << prio);
    task->runq_next = NULL;

    if (rq->queues[prio] == NULL) {
        rq->queues[prio] = task;
    } else {
        rq->queue_tails[prio]->runq_next = task;
    }
    rq->queue_tails[prio] = task;
    rq->count++;
}

kern_task_t *kern_runq_dequeue(kern_runq_t *rq)
{
    if (rq->bitmap == 0) return NULL;

    /* Find highest priority with ready tasks */
    uint8_t prio = (uint8_t)__builtin_ctz(rq->bitmap);
    if (prio >= KERN_PRIO_MAX) return NULL;

    kern_task_t *task = rq->queues[prio];
    if (task == NULL) return NULL;

    rq->queues[prio] = task->runq_next;

    if (rq->queues[prio] == NULL) {
        rq->bitmap &= ~(1u << prio);
        rq->queue_tails[prio] = NULL;
    }
    rq->count--;
    task->runq_next = NULL;
    return task;
}

bool kern_runq_remove(kern_runq_t *rq, kern_task_t *task)
{
    uint8_t prio = task->priority;
    if (prio >= KERN_PRIO_MAX) prio = KERN_PRIO_IDLE;

    kern_task_t **prev = &rq->queues[prio];
    kern_task_t *curr = rq->queues[prio];

    while (curr != NULL) {
        if (curr == task) {
            *prev = curr->runq_next;
            if (curr->runq_next == NULL) {
                rq->queue_tails[prio] = (prev == &rq->queues[prio]) ? NULL : *prev;
            }
            if (rq->queues[prio] == NULL) {
                rq->bitmap &= ~(1u << prio);
            }
            rq->count--;
            task->runq_next = NULL;
            return true;
        }
        prev = &curr->runq_next;
        curr = curr->runq_next;
    }
    return false;
}

bool kern_runq_contains(kern_runq_t *rq, kern_task_t *task)
{
    return (task->runq_next != NULL) || (task == rq->queues[task->priority]);
}
```

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_runq.c && git commit -m "feat(runq): implement O(1) priority bitmap ready queue"
```

---

### Task 2.3: Update `kern_types.h` — add priority constants and new task fields

**Files:**
- Modify: `src/kernel/kern_types.h`

- [ ] **Step 1: Add affinity constant**

In `src/kernel/kern_types.h`, after `#define KERN_CPU_ANY 0xFF` (line 50), add:

```c
#define KERN_AFFINITY_ANY    0x03   /* allow both cores (bit0=Core0, bit1=Core1) */
#define KERN_TASK_FLAG_PINNED 0x02  /* task is pinned to a specific core */
```

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_types.h && git commit -m "feat(types): add affinity and pinned flag constants"
```

---

### Task 2.4: Update `kern_task.h` — add runq_next, sleep_next, affinity fields

**Files:**
- Modify: `src/kernel/kern_task.h`

- [ ] **Step 1: Add new TCB fields**

In the `kern_task_t` struct (after `cpu_id` around line 81), modify the existing block and add:

```c
    /* SMP affinity */
    uint8_t             cpu_id;         /* current CPU (KERN_CPU_ANY=auto) */
    uint8_t             affinity_mask;  /* allowed cores bitmap */

    /* List pointers */
    struct kern_task   *next;           /* global task list */
    struct kern_task   *runq_next;      /* runqueue link (per-CPU, different from global next) */
    struct kern_task   *sleep_next;     /* sleep list link */
    struct kern_task   *wait_next;      /* mutex/sem wait queue link */
```

Note: The existing `next` field stays for the global task list. The new `runq_next` is separate because a task can be on exactly one runqueue at a time, and the global list is orthogonal.

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_task.h && git commit -m "feat(task): add runq_next, sleep_next, affinity fields to TCB"
```

---

### Task 2.5: Rewrite `kern_sched.c` — new bare-metal scheduler

**Files:**
- Rewrite: `src/kernel/kern_sched.c`

- [ ] **Step 1: Write the new bare-metal scheduler**

Due to length, this is the most complex file. The key structure:

```c
/**
 * @file   kern_sched.c
 * @brief  Xeros bare-metal priority preemptive scheduler
 */

#include "kern_sched.h"
#include "kern_sched_class.h"
#include "kern_sched_rr.h"
#include "kern_sched_fifo.h"
#include "kern_task.h"
#include "kern_cpu.h"
#include "kern_runq.h"
#include "kern_ctx.h"
#include "kern_init.h"
#include "kern_mpu.h"
#include "kern_kmalloc.h"

#include <string.h>
#include <stdlib.h>

#if defined(NATIVE_TEST)
#include <ucontext.h>
#elif defined(XEROS_NATIVE_SCHED)
#include "kern_ctx_esp32.h"
#include <setjmp.h>
#elif defined(XEROS_BARE_METAL)
/* bare-metal: kern_ctx.h already included via kern_cpu.h */
#endif

/* ─── IDLE config ─── */
#define IDLE_STACK_MIN  2048

/* ─── Global scheduler state ─── */
kern_task_t   *g_task_list = NULL;
kern_task_t   *g_task_list_tail = NULL;
kern_pid_t     g_next_pid = 0;
uint8_t        g_task_count = 0;
static bool    g_sched_initialized = false;

#ifdef CONFIG_SMP_ENABLED
volatile bool  g_task_list_lock = false;
#endif

/* ... Native test and native sched paths unchanged ... */

#elif defined(XEROS_BARE_METAL)
/* ═══════════════════ BARE-METAL SCHEDULER ═══════════════════ */

/* Idle task stack buffers (statically allocated — must remain valid) */
static uint8_t s_idle_stack0[IDLE_STACK_MIN] __attribute__((aligned(16)));
#ifdef CONFIG_SMP_ENABLED
static uint8_t s_idle_stack1[IDLE_STACK_MIN] __attribute__((aligned(16)));
#endif

/* idle task entry — just yields forever */
static void bare_idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        /* In bare-metal, yield goes through kern_ctx_switch back to sched loop */
        kern_yield();
    }
}

void kern_sched_init(void)
{
    if (g_sched_initialized) return;
    g_sched_initialized = true;
    g_task_list_tail = NULL;

    /* Initialize per-CPU structures */
    for (uint8_t i = 0; i < KERN_MAX_CPUS; i++) {
        g_per_cpu[i].cpu_id       = i;
        g_per_cpu[i].current_task = NULL;
        g_per_cpu[i].idle_task    = NULL;
        g_per_cpu[i].sched_ticks  = 0;
        g_per_cpu[i].need_resched = false;
        g_per_cpu[i].task_count   = 0;
    }

    /* Register default scheduling classes */
    kern_sched_class_register(&sched_class_rr);
    kern_sched_class_register(&sched_class_fifo);

    /* Create per-CPU idle tasks with bare-metal contexts */
    for (uint8_t cpu = 0; cpu < KERN_MAX_CPUS; cpu++) {
        char name[16];
        snprintf(name, sizeof(name), "xidle%d", cpu);

        kern_task_t *idle = (kern_task_t *)calloc(1, sizeof(kern_task_t));
        if (idle == NULL) {
            kern_panic("failed to allocate idle task");
            return;
        }

        idle->pid = g_next_pid++;
        idle->state = KERN_TASK_READY;
        idle->priority = KERN_PRIO_IDLE;
        idle->cpu_id = cpu;
        idle->affinity_mask = (1u << cpu);  /* pinned to this core */
        strncpy(idle->name, name, KERN_TASK_NAME_LEN);
        idle->entry = bare_idle_entry;
        idle->arg = NULL;
        idle->stack_size = IDLE_STACK_MIN;

        /* Allocate stack and init context */
        uint8_t *stack = (cpu == 0) ? s_idle_stack0 : s_idle_stack1;
        idle->stack_base = stack;
        void *stack_top = stack + IDLE_STACK_MIN;
        kern_ctx_init(&idle->ctx, bare_idle_entry, NULL, stack_top);

        /* Add to global task list */
        idle->next = g_task_list;
        g_task_list = idle;
        if (g_task_list_tail == NULL) g_task_list_tail = idle;
        idle->scheduler_class_id = KERN_SCHED_CLASS_RR_ID;
        g_task_count++;

        /* Set as this core's idle and current task */
        g_per_cpu[cpu].idle_task = idle;
        g_per_cpu[cpu].current_task = idle;
        g_per_cpu[cpu].task_count = 1;

        /* Save idle context */
        kern_ctx_init(&g_per_cpu[cpu].idle_ctx, NULL, NULL, NULL);
    }

    kern_log(KERN_LOG_INFO, "scheduler initialized (bare-metal, %d cores)", KERN_MAX_CPUS);
}

void kern_sched_tick(void)
{
    if (!g_sched_initialized) return;

    uint8_t cpu = KERN_THIS_CPU;
    g_per_cpu[cpu].sched_ticks++;

    reap_zombies();

    /* Dispatch tick to scheduling classes */
    for (int i = 0; i < g_sched_class_count; i++) {
        if (g_sched_classes[i] && g_sched_classes[i]->tick) {
            g_sched_classes[i]->tick(g_current_task);
        }
    }

    /* Check for stack pressure and memory pressure */
    sched_check_stack_pressure(g_current_task);
    sched_notify_memory_pressure();

    /* Check if reschedule is needed */
    if (g_need_resched || (g_current_task &&
        g_current_task->state != KERN_TASK_RUNNING)) {
        g_need_resched = false;

        kern_task_t *current = g_current_task;

        /* If current task is still runnable, re-enqueue it */
        if (current && current->state == KERN_TASK_RUNNING) {
            current->state = KERN_TASK_READY;
            kern_runq_enqueue(&g_per_cpu[cpu].runq, current);
        }

        /* Pick next ready task */
        kern_task_t *next = kern_runq_dequeue(&g_per_cpu[cpu].runq);
        if (next == NULL) {
            /* Nothing ready — switch to idle */
            next = g_per_cpu[cpu].idle_task;
        }

        g_per_cpu[cpu].current_task = next;
        next->state = KERN_TASK_RUNNING;

#ifdef CONFIG_MPU_ENABLED
        kern_mpu_apply(next);
#endif

        /* Context switch from idle_ctx (scheduler) to task */
        kern_ctx_switch(&g_per_cpu[cpu].idle_ctx, &next->ctx);
    }
}

#endif /* XEROS_BARE_METAL */

/* ─── idle_entry (used by native paths, kept for compatibility) ─── */
void idle_entry(void *arg)
{
    (void)arg;
    while (1) { kern_yield(); }
}
```

- [ ] **Step 2: Implement kern_yield for bare-metal**

In `src/kernel/kern_task.c`, find the `kern_yield` implementation. After the existing paths, add the bare-metal path:

```c
#elif defined(XEROS_BARE_METAL)

void kern_yield(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    kern_task_t *task = g_per_cpu[cpu].current_task;

    if (task == NULL || task == g_per_cpu[cpu].idle_task) return;

    /* Save current state and jump back to scheduler */
    task->state = KERN_TASK_READY;
    kern_runq_enqueue(&g_per_cpu[cpu].runq, task);
    kern_ctx_switch(&task->ctx, &g_per_cpu[cpu].idle_ctx);
    /* Returns here when rescheduled */
}
```

- [ ] **Step 3: Commit**

```bash
git add src/kernel/kern_sched.c src/kernel/kern_task.c
git commit -m "feat(sched): implement bare-metal priority preemptive scheduler

- kern_sched_init: creates per-CPU idle tasks with kern_ctx_init
- kern_sched_tick: picks next task from runqueue, ctx_switch to it
- kern_yield: re-enqueues current, switches back to scheduler loop
- Uses kern_ctx_switch for real context switching (no semaphore protocol)"
```

---

### Task 2.6: Update `kern_task.c` — bare-metal spawn and exit

**Files:**
- Modify: `src/kernel/kern_task.c`

- [ ] **Step 1: Add bare-metal spawn**

In `kern_spawn`, after the existing paths (near the FreeRTOS spawn code), add:

```c
#elif defined(XEROS_BARE_METAL)

kern_pid_t kern_spawn(const char *name, void (*entry)(void *arg),
                       void *arg, size_t stack_min)
{
    if (!g_sched_initialized) return KERN_ENOMEM;
    if (g_task_count >= KERN_MAX_TASKS) return KERN_ENOMEM;

    /* Allocate TCB */
    kern_task_t *task = (kern_task_t *)calloc(1, sizeof(kern_task_t));
    if (task == NULL) return KERN_ENOMEM;

    /* Determine CPU assignment */
    uint8_t cpu = kern_smp_assign_cpu(task);

    /* Name */
    if (name) {
        strncpy(task->name, name, KERN_TASK_NAME_LEN);
    } else {
        snprintf(task->name, KERN_TASK_NAME_LEN, "task%d", g_next_pid);
    }

    /* Allocate stack */
    size_t stack_size = stack_min;
    if (stack_size < KERN_STACK_MIN) stack_size = KERN_STACK_MIN;
    if (stack_size > KERN_STACK_MAX) stack_size = KERN_STACK_MAX;

    task->stack_base = (uint8_t *)heap_caps_malloc(stack_size, MALLOC_CAP_8BIT);
    if (task->stack_base == NULL) {
        free(task);
        return KERN_ENOMEM;
    }
    task->stack_size = stack_size;

    /* Initialize context */
    void *stack_top = task->stack_base + stack_size;
    kern_ctx_init(&task->ctx, entry, arg, stack_top);

    /* Set TCB fields */
    task->pid = g_next_pid++;
    task->state = KERN_TASK_READY;
    task->priority = KERN_PRIO_DEFAULT;
    task->cpu_id = cpu;
    task->affinity_mask = KERN_AFFINITY_ANY;
    task->entry = entry;
    task->arg = arg;

    /* Add to global list */
    task->next = g_task_list;
    g_task_list = task;
    if (g_task_list_tail == NULL) g_task_list_tail = task;
    g_task_count++;

    /* Add to per-CPU runqueue */
    kern_runq_enqueue(&g_per_cpu[cpu].runq, task);
    g_per_cpu[cpu].task_count++;

    /* Mark reschedule if higher priority */
    kern_task_t *current = g_per_cpu[cpu].current_task;
    if (current && task->priority < current->priority) {
        g_per_cpu[cpu].need_resched = true;
    }

    return task->pid;
}
```

- [ ] **Step 2: Add bare-metal exit**

```c
#elif defined(XEROS_BARE_METAL)

void kern_exit(void)
{
    uint8_t cpu = KERN_THIS_CPU;
    kern_task_t *task = g_per_cpu[cpu].current_task;

    if (task == NULL) return;

    /* Mark as zombie — reap_zombies will clean up */
    task->state = KERN_TASK_ZOMBIE;
    g_per_cpu[cpu].need_resched = true;

    /* Return to scheduler (which will reap and pick next) */
    kern_ctx_switch(&task->ctx, &g_per_cpu[cpu].idle_ctx);
    /* Never returns */
}
```

- [ ] **Step 3: Commit**

```bash
git add src/kernel/kern_task.c && git commit -m "feat(task): implement bare-metal kern_spawn and kern_exit

- kern_spawn: allocates TCB + stack, initializes kern_ctx, enqueues to runqueue
- kern_exit: marks ZOMBIE, switches back to scheduler for cleanup
- Replaces FreeRTOS xTaskCreatePinnedToCore / vTaskDelete path"
```

---

## Phase 3: Timer and Sleep

### Task 3.1: Create `kern_timer.h` — timer interface

**Files:**
- Create: `src/kernel/kern_timer.h`

```c
/**
 * @file   kern_timer.h
 * @brief  Bare-metal timer subsystem interface (CCOMPARE0)
 */

#ifndef KERN_TIMER_H
#define KERN_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the per-CPU CCOMPARE0 timer.
 * Must be called on each core before enabling interrupts. */
void kern_timer_init_per_cpu(uint8_t cpu);

/* Get current tick count for this CPU */
uint64_t kern_ticks(void);

/* Convert ticks to microseconds (approximate) */
uint32_t kern_ticks_to_us(uint64_t ticks);

/* Convert microseconds to ticks */
uint64_t kern_us_to_ticks(uint32_t us);

/* Software timer callback type */
typedef void (*kern_soft_timer_cb_t)(void *arg);

/* Software timer descriptor */
typedef struct kern_soft_timer {
    uint64_t              expire_tick;
    kern_soft_timer_cb_t  callback;
    void                 *arg;
    bool                  periodic;
    uint32_t              period_ticks;
    struct kern_soft_timer *next;
} kern_soft_timer_t;

/* Add a one-shot software timer. Returns 0 on success. */
int kern_soft_timer_add(kern_soft_timer_t *timer);

/* Add a periodic software timer. */
int kern_soft_timer_add_periodic(kern_soft_timer_t *timer, uint32_t period_ms);

/* Cancel a software timer (must be called before timer fires). */
void kern_soft_timer_cancel(kern_soft_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* KERN_TIMER_H */
```

- [ ] **Step 1: Commit (header only first)**

```bash
git add src/kernel/kern_timer.h && git commit -m "feat(timer): add bare-metal timer interface header"
```

---

### Task 3.2: Create `kern_timer.c` — CCOMPARE0 timer implementation

**Files:**
- Create: `src/kernel/kern_timer.c`

This is a large file. Key implementation notes:

```c
/**
 * @file   kern_timer.c
 * @brief  Bare-metal timer using Xtensa CCOMPARE0 (per-core)
 *
 * ESP32 CCOUNT register increments every CPU cycle (240MHz = 240 counts/us).
 * CCOMPARE0 fires an interrupt when CCOUNT == CCOMPARE0.
 *
 * ISR: re-arms timer, increments tick, decrements timeslice,
 *      sets need_resched on timeslice expiry, processes soft timers.
 */

#include "kern_timer.h"
#include "kern_cpu.h"
#include "kern_task.h"
#include "kern_ctx.h"

#include "esp_attr.h"      /* IRAM_ATTR */
#include "esp_cpu.h"       /* esp_cpu_get_cycle_count() */

/* Xtensa interrupt number for CCOMPARE0 */
#define ETS_CCOMPARE0_INUM  6

/* Tick rate: 1000 Hz = 1ms */
#define TICK_HZ 1000

/* ─── static state ─── */
static uint32_t s_ticks_per_ms;               /* CPU cycles per 1ms */
static kern_soft_timer_t *s_timer_head = NULL; /* sorted by expire_tick */

/* ─── CPU frequency detection ─── */
static uint32_t get_cpu_freq_mhz(void)
{
    /* ESP32 runs at 240MHz by default (sdkconfig.defaults specifies this) */
    return 240;
}

void kern_timer_init_per_cpu(uint8_t cpu)
{
    s_ticks_per_ms = get_cpu_freq_mhz() * 1000;  /* 240,000 */

    /* Set initial CCOMPARE0 */
    uint32_t ccount;
    __asm__ volatile("rsr.ccount %0" : "=r"(ccount));
    __asm__ volatile("wsr.ccompare0 %0" :: "r"(ccount + s_ticks_per_ms));

    /* Enable CCOMPARE0 interrupt (level 1, low priority) */
    uint32_t mask = 1u << ETS_CCOMPARE0_INUM;
    uint32_t intenable;
    __asm__ volatile("rsr.intenable %0" : "=r"(intenable));
    intenable |= mask;
    __asm__ volatile("wsr.intenable %0" :: "r"(intenable));
}

/* CCOMPARE0 ISR — called on every 1ms tick */
void IRAM_ATTR kern_timer_isr(void *arg)
{
    (void)arg;
    uint8_t cpu = kern_cpu_id();

    /* Re-arm timer */
    uint32_t ccount;
    __asm__ volatile("rsr.ccount %0" : "=r"(ccount));
    __asm__ volatile("wsr.ccompare0 %0" :: "r"(ccount + s_ticks_per_ms));

    /* Clear interrupt */
    uint32_t mask = 1u << ETS_CCOMPARE0_INUM;
    __asm__ volatile("wsr.intclear %0" :: "r"(mask));

    /* Increment tick */
    g_per_cpu[cpu].sched_ticks++;

    /* Timeslice countdown */
    kern_task_t *cur = g_per_cpu[cpu].current_task;
    if (cur && cur != g_per_cpu[cpu].idle_task) {
        if (cur->timeslice_remaining > 0) {
            cur->timeslice_remaining--;
            if (cur->timeslice_remaining == 0) {
                g_per_cpu[cpu].need_resched = true;
            }
        }
    }

    /* Software timers (Core 0 only to avoid race) */
    if (cpu == 0) {
        uint64_t now = g_per_cpu[0].sched_ticks;
        while (s_timer_head && s_timer_head->expire_tick <= now) {
            kern_soft_timer_t *t = s_timer_head;
            s_timer_head = t->next;
            if (t->callback) t->callback(t->arg);
            if (t->periodic) {
                t->expire_tick = now + t->period_ticks;
                /* Re-insert sorted */
                /* ... insertion logic ... */
            }
        }
    }
}

uint64_t kern_ticks(void)
{
    return g_per_cpu[KERN_THIS_CPU].sched_ticks;
}

uint32_t kern_ticks_to_us(uint64_t ticks)
{
    return (uint32_t)(ticks * 1000);  /* each tick = 1ms = 1000us */
}

uint64_t kern_us_to_ticks(uint32_t us)
{
    return (us + 999) / 1000;  /* round up to nearest ms */
}
```

- [ ] **Step 1: Commit**

```bash
git add src/kernel/kern_timer.c && git commit -m "feat(timer): implement CCOMPARE0 per-core tick timer with ISR"
```

---

### Task 3.3: Create `kern_sleep.c` — sleep/wakeup

**Files:**
- Create: `src/kernel/kern_sleep.c`

```c
/**
 * @file   kern_sleep.c
 * @brief  Task sleep/wakeup — replacement for vTaskDelay
 */

#include "kern_task.h"
#include "kern_cpu.h"
#include "kern_timer.h"
#include "kern_runq.h"

#include <stddef.h>

/* Sleep list sorted by wake_tick (earliest first) */
static kern_task_t *s_sleep_list = NULL;

void kern_sleep_ms(uint32_t ms)
{
    uint8_t cpu = KERN_THIS_CPU;
    kern_task_t *task = g_per_cpu[cpu].current_task;

    if (task == NULL || task == g_per_cpu[cpu].idle_task) return;
    if (ms == 0) {
        kern_yield();
        return;
    }

    /* Set sleep state and wake time */
    task->state = KERN_TASK_SLEEPING;
    task->wake_time = g_per_cpu[cpu].sched_ticks + kern_us_to_ticks(ms * 1000);

    /* Insert into sorted sleep list */
    kern_task_t **prev = &s_sleep_list;
    while (*prev && (*prev)->wake_time <= task->wake_time) {
        prev = &(*prev)->sleep_next;
    }
    task->sleep_next = *prev;
    *prev = task;

    /* Yield — scheduler will pick next ready task */
    kern_yield();
}

/* Called by sched_loop each tick to wake expired sleepers */
void kern_sleep_tick(uint8_t cpu)
{
    uint64_t now = g_per_cpu[cpu].sched_ticks;

    while (s_sleep_list && s_sleep_list->wake_time <= now) {
        kern_task_t *t = s_sleep_list;
        s_sleep_list = t->sleep_next;
        t->sleep_next = NULL;

        t->state = KERN_TASK_READY;
        kern_runq_enqueue(&g_per_cpu[t->cpu_id].runq, t);

        /* If woken task has higher priority, mark resched */
        kern_task_t *cur = g_per_cpu[t->cpu_id].current_task;
        if (cur && t->priority < cur->priority) {
            g_per_cpu[t->cpu_id].need_resched = true;
        }
    }
}
```

- [ ] **Step 1: Commit**

```bash
git add src/kernel/kern_sleep.c && git commit -m "feat(sleep): implement kern_sleep_ms with sorted wakeup list"
```

---

## Phase 4: Sync Primitives and API Migration

### Task 4.1: Rewrite `kern_sync.c` — mutex/sem with real blocking

**Files:**
- Rewrite: `src/kernel/kern_sync.c`

The existing mutex_lock spin-waits (busy loop). Replace with kern_yield-based blocking:

- [ ] **Step 1: Rewrite mutex_lock to use kern_yield when contended**

In the `else` branch (contended case), replace the spin-wait loop:

```c
    } else {
        /* Contended: block via yield instead of spinning */
        /* Add to wait queue */
        self->wait_next = m->wait_queue;
        m->wait_queue = self;
        spinlock_unlock(&m->lock);

        /* Block until woken */
        self->state = KERN_TASK_BLOCKED;
        kern_yield();

        /* When woken, the lock is ours. No need to re-check. */
        return KERN_OK;
    }
```

Similarly update mutex_unlock to wake waiters via runqueue enqueue instead of just clearing owner.

- [ ] **Step 2: Add semaphore implementation**

```c
kern_err_t sem_init(sem_t *s, uint32_t initial, uint32_t max)
{
    spinlock_init(&s->guard);
    s->count = (int32_t)initial;
    s->max_count = max;
    s->waiters = NULL;
    return KERN_OK;
}

kern_err_t sem_wait(sem_t *s)
{
    kern_task_t *self = g_current_task;
    spinlock_lock(&s->guard);

    if (s->count > 0) {
        s->count--;
        spinlock_unlock(&s->guard);
        return KERN_OK;
    }

    /* Block */
    self->wait_next = s->waiters;
    s->waiters = self;
    spinlock_unlock(&s->guard);
    self->state = KERN_TASK_BLOCKED;
    kern_yield();
    return KERN_OK;
}

kern_err_t sem_post(sem_t *s)
{
    spinlock_lock(&s->guard);
    if (s->waiters) {
        kern_task_t *w = s->waiters;
        s->waiters = w->wait_next;
        w->wait_next = NULL;
        w->state = KERN_TASK_READY;
        kern_runq_enqueue(&g_per_cpu[w->cpu_id].runq, w);
    } else if ((uint32_t)s->count < s->max_count) {
        s->count++;
    }
    spinlock_unlock(&s->guard);
    return KERN_OK;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/kernel/kern_sync.c && git commit -m "refactor(sync): mutex/sem use kern_yield blocking instead of spin-wait

- mutex_lock: contested path now calls kern_yield (BLOCKED) instead of busy loop
- mutex_unlock: wakes next waiter via runqueue enqueue
- Added semaphore implementation (sem_init, sem_wait, sem_post)"
```

---

### Task 4.2: Migrate `main.cpp` — remove FreeRTOS dependencies

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace FreeRTOS includes**

At the top of the `#ifndef NATIVE_TEST` block (line 22-25), replace:

```cpp
#ifndef NATIVE_TEST

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
```

With:

```cpp
#ifndef NATIVE_TEST

#include "esp_heap_caps.h"
#include "esp_timer.h"

#if !defined(XEROS_BARE_METAL)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
```

- [ ] **Step 2: Replace vTaskDelay in main loop**

At the end of the main loop (line 274), change:

```cpp
        vTaskDelay(pdMS_TO_TICKS(1));
```

To:

```cpp
#if defined(XEROS_BARE_METAL)
        kern_sleep_ms(1);
        /* In bare-metal mode, the scheduler loop is called from timer ISR */
#else
        vTaskDelay(pdMS_TO_TICKS(1));
#endif
```

- [ ] **Step 3: Conditionally compile deferred_kernel_init for bare-metal**

In `deferred_kernel_init()`, after `hal_delay_ms(10)` (line 386), add:

```cpp
#if defined(XEROS_BARE_METAL)
    /* Bare-metal: the scheduler loop runs on this (Core 0) thread.
     * We don't return from here — the timer ISR drives scheduling. */
    kern_log(KERN_LOG_INFO, "Xeros bare-metal kernel entering scheduler");
    for (;;) {
        /* The timer ISR handles scheduling ticks.
         * Main loop just processes I/O and sleeps. */
        dev_ttyS0_poll();
        serial_monitor_update();
        wifi_mgr_process_requests();
        kern_sleep_ms(1);
    }
#endif
```

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp && git commit -m "refactor(main): conditionally remove FreeRTOS dependencies for bare-metal"
```

---

### Task 4.3: Migrate app and HAL files — replace vTaskDelay

**Files:**
- Modify: `src/hal/hal_system.cpp`
- Modify: `src/app/ui_task.c`
- Modify: `src/app/wifi/wifi_manager.cpp`
- Modify: `src/app/token_usage/tu_api.cpp`

- [ ] **Step 1: Replace vTaskDelay in hal_system.cpp**

In `src/hal/hal_system.cpp`, find `vTaskDelay` calls and replace:

```cpp
// Before:
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
void hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// After:
#if defined(XEROS_BARE_METAL)
#include "kernel/kern_task.h"
void hal_delay_ms(uint32_t ms) {
    kern_sleep_ms(ms);
}
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
void hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
#endif
```

- [ ] **Step 2: Replace vTaskDelay in ui_task.c**

In `src/app/ui_task.c`:

```c
// Before:
vTaskDelay(pdMS_TO_TICKS(50));

// After:
#if defined(XEROS_BARE_METAL)
    kern_sleep_ms(50);
#else
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
```

- [ ] **Step 3: Replace vTaskDelay in wifi_manager.cpp**

Same pattern as above, replace `vTaskDelay(pdMS_TO_TICKS(n))` with conditional `kern_sleep_ms(n)`.

- [ ] **Step 4: Replace vTaskDelay in tu_api.cpp**

Same pattern.

- [ ] **Step 5: Commit**

```bash
git add src/hal/hal_system.cpp src/app/ui_task.c src/app/wifi/wifi_manager.cpp src/app/token_usage/tu_api.cpp
git commit -m "refactor(api): conditionally replace vTaskDelay with kern_sleep_ms"
```

---

## Phase 5: Dual-Core SMP

### Task 5.1: Rewrite `kern_smp.c` — dual-core boot and IPI

**Files:**
- Rewrite: `src/kernel/kern_smp.c`

- [ ] **Step 1: Write bare-metal SMP implementation**

```c
/**
 * @file   kern_smp.c
 * @brief  Bare-metal dual-core management
 *
 * Core 0 boot:
 *   app_main → kern_init → kern_sched_init → create idle0 → start core1
 *
 * Core 1 boot:
 *   esp_rom_aps_digi_boot → kern_smp_entry_core1 → kern_timer_init → sched_loop
 */

#include "kern_smp.h"
#include "kern_cpu.h"
#include "kern_task.h"
#include "kern_runq.h"
#include "kern_timer.h"
#include "kern_ctx.h"
#include "kern_init.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "soc/rtc.h"

#if defined(XEROS_BARE_METAL)

static volatile bool s_core1_ready = false;

/* Core 1 entry point (called from ROM boot code) */
static void kern_smp_sched_loop_core1(void *arg)
{
    (void)arg;

    kern_log(KERN_LOG_INFO, "SMP: core 1 scheduler entering loop");

    /* Initialize Core 1 timer */
    kern_timer_init_per_cpu(1);

    s_core1_ready = true;

    /* Core 1 scheduler loop */
    for (;;) {
        kern_sched_tick();
        /* Low-power idle: wait for interrupt */
        __asm__ volatile("waiti 0");
    }
}

void kern_smp_start_core1(void)
{
    kern_log(KERN_LOG_INFO, "SMP: starting core 1 via esp_rom_aps_digi_boot");

    /* ESP32 ROM function to boot APP_CPU */
    extern void esp_rom_aps_digi_boot(void);
    esp_rom_aps_digi_boot();
}

/* IPI via Xtensa software interrupt */
#define IPI_SOFT_INTR_CORE0  0x01  /* bit 0: Core 0 software interrupt */
#define IPI_SOFT_INTR_CORE1  0x02  /* bit 1: Core 1 software interrupt */

void kern_smp_send_ipi(uint8_t target_cpu)
{
    if (target_cpu == 0) {
        __asm__ volatile("wsr.intset %0" :: "r"(IPI_SOFT_INTR_CORE0));
    } else {
        __asm__ volatile("wsr.intset %0" :: "r"(IPI_SOFT_INTR_CORE1));
    }
}

/* IPI ISR — triggers reschedule on target core */
void IRAM_ATTR kern_ipi_isr(void *arg)
{
    (void)arg;
    uint8_t cpu = kern_cpu_id();

    /* Clear software interrupt */
    if (cpu == 0) {
        __asm__ volatile("wsr.intclear %0" :: "r"(IPI_SOFT_INTR_CORE0));
    } else {
        __asm__ volatile("wsr.intclear %0" :: "r"(IPI_SOFT_INTR_CORE1));
    }

    g_per_cpu[cpu].ipi_pending = true;
    g_per_cpu[cpu].need_resched = true;
}

/* CPU assignment with load balancing */
uint8_t kern_smp_assign_cpu(kern_task_t *task)
{
    if (task->affinity_mask == 0) {
        task->affinity_mask = KERN_AFFINITY_ANY;
    }

    if (task->affinity_mask == 1) return 0;   /* Core 0 only */
    if (task->affinity_mask == 2) return 1;   /* Core 1 only */

    /* Load balance: assign to core with fewer tasks */
    return (g_per_cpu[0].task_count <= g_per_cpu[1].task_count) ? 0 : 1;
}

#endif /* XEROS_BARE_METAL */
```

- [ ] **Step 2: Commit**

```bash
git add src/kernel/kern_smp.c && git commit -m "feat(smp): implement bare-metal dual-core boot, IPI, and task affinity"
```

---

### Task 5.2: Rewrite `kern_port_freertos.c` → `kern_port_bare.c`

**Files:**
- Rename: `src/kernel/kern_port_freertos.c` → `src/kernel/kern_port_bare.c`
- Content: empty port layer (all scheduling now handled directly in kern_sched/kern_ctx)

- [ ] **Step 1: Create bare-metal port**

The port layer now only handles the `kern_port_ops_t` table with stub/no-op implementations since the bare-metal kernel handles scheduling directly.

```c
/**
 * @file   kern_port_bare.c
 * @brief  Bare-metal port layer — delegates to kern_ctx / kern_sched directly
 */

#include "kern_port.h"

#if defined(XEROS_BARE_METAL) && !defined(NATIVE_TEST)

static void bare_port_init(void) {}
static kern_port_thread_t bare_thread_spawn(...) { return KERN_PORT_THREAD_NULL; }
static void bare_thread_exit(void) { while(1){} }
static void bare_thread_kill(kern_port_thread_t t) { (void)t; }
static size_t bare_thread_stack_usage(kern_port_thread_t t) { (void)t; return 0; }
static void bare_switch_to(kern_task_t *t) { (void)t; }
static void bare_task_yield(void) {}
static void bare_task_exit(void) { while(1){} }
static void bare_idle(void) { __asm__ volatile("waiti 0"); }
static int bare_timer_set(uint32_t p) { (void)p; return 0; }
static void bare_timer_stop(void) {}
static bool bare_preempt_consume(void) { return false; }

const kern_port_ops_t g_kern_port_ops = {
    .init               = bare_port_init,
    .thread_spawn       = bare_thread_spawn,
    .thread_exit        = bare_thread_exit,
    .thread_kill        = bare_thread_kill,
    .thread_stack_usage = bare_thread_stack_usage,
    .switch_to          = bare_switch_to,
    .task_yield         = bare_task_yield,
    .task_exit          = bare_task_exit,
    .idle               = bare_idle,
    .timer_set_periodic = bare_timer_set,
    .timer_stop         = bare_timer_stop,
    .preempt_consume    = bare_preempt_consume,
};

#endif /* XEROS_BARE_METAL */
```

- [ ] **Step 2: Commit**

```bash
git rm src/kernel/kern_port_freertos.c
git add src/kernel/kern_port_bare.c
git commit -m "refactor(port): replace FreeRTOS port with bare-metal stub port layer"
```

---

### Task 5.3: Update `sdkconfig.defaults` — disable FreeRTOS

**Files:**
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Disable FreeRTOS options**

```ini
# Disable FreeRTOS (bare-metal kernel handles scheduling)
CONFIG_FREERTOS_UNICORE=n
# CONFIG_FREERTOS_HZ=100  # Remove or comment out
CONFIG_FREERTOS_ENABLED=n

# Enable bare-metal kernel
CONFIG_XEROS_BARE_METAL=y

# Keep ESP-IDF components that don't require FreeRTOS
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y
CONFIG_SPI_FLASH_ENABLED=y
```

- [ ] **Step 2: Commit**

```bash
git add sdkconfig.defaults && git commit -m "config: disable FreeRTOS, enable XEROS_BARE_METAL in sdkconfig"
```

---

### Task 5.4: Add build flag for XEROS_BARE_METAL

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Add build flag**

```ini
[env:m5stick-c]
platform = espressif32
board = m5stick-c
framework = espidf
build_flags =
    -D XEROS_BARE_METAL
    -D CONFIG_PREEMPT_ENABLED
    -D CONFIG_SMP_ENABLED
```

- [ ] **Step 2: Commit**

```bash
git add platformio.ini && git commit -m "build: add XEROS_BARE_METAL compile flag for bare-metal kernel"
```

---

## Verification and Testing

### Task 6: Build verification and hardware test

- [ ] **Step 1: Clean build**

```bash
cd /Users/yukisala/subject/Xerintosh && pio run -e m5stick-c -t clean && pio run -e m5stick-c
```

Expected: Build succeeds with zero errors. Warnings are acceptable but should be reviewed.

- [ ] **Step 2: Run native tests**

```bash
cd /Users/yukisala/subject/Xerintosh && pio test -e native 2>&1 | tail -20
```

Expected: All native tests pass. Note that some tests may depend on FreeRTOS stubs — those tests should be conditionally disabled for bare-metal.

- [ ] **Step 3: Flash to hardware and verify boot**

```bash
cd /Users/yukisala/subject/Xerintosh && pio run -e m5stick-c -t upload && pio device monitor
```

Expected output:
```
[  BOOT] M5Stick-P1 kernel starting...
[  OK  ] UART initialized
[  OK  ] NVS storage
...
[  OK  ] Kernel subsystems, free_heap=...
[  OK  ] Shell spawned on /dev/ttyS0
[  OK  ] UI task spawned (pid=2, stack=4096)
[  OK  ] WiFi manager spawned as kernel task (stack=4096)
SMP: starting core 1 via esp_rom_aps_digi_boot
SMP: core 1 scheduler entering loop
Xeros bare-metal kernel entering scheduler
```

- [ ] **Step 4: Commit verification results**

```bash
git commit --allow-empty -m "verify: bare-metal kernel build and boot test results"
```

---

## Rollback Plan

If the bare-metal kernel fails to boot on hardware:

1. `git checkout main` — return to FreeRTOS-based kernel
2. `pio run -e m5stick-c -t upload` — restore working firmware
3. Debug with JTAG/serial output from bare-metal branch

---

## Phase Completion Checklist

- [ ] Phase 1: kern_ctx.h, kern_ctx_esp32.S, kern_cpu.h, kern_cpu.c — context switching compiles
- [ ] Phase 2: kern_runq.h/.c, updated kern_sched.c, kern_task.c — scheduler functions
- [ ] Phase 3: kern_timer.h/.c, kern_sleep.c — timer and sleep work
- [ ] Phase 4: kern_sync.c rewrite, API migration in main/hal/app — no more FreeRTOS calls
- [ ] Phase 5: kern_smp.c rewrite, kern_port_bare.c, sdkconfig — dual-core boots
- [ ] Verification: build succeeds, native tests pass, hardware boots
