/* Only compile for Xtensa targets (ESP32), skip on native test builds
 * and XEROS_NATIVE_SCHED fallback builds (which use FreeRTOS tasks). */
#if !defined(NATIVE_TEST) && !defined(XEROS_NATIVE_SCHED)

/**
 * @file ctx_switch.S
 * @brief Xeros context switch engine -- Xtensa implementation for ESP32
 *
 * Context save/restore design:
 *
 * xeros_ctx_save: standard call8 ABI function with `entry` / `retw`.
 *   - Called from C via call8; ctx is in a10 before entry and a2 after entry.
 *   - Saves the callee-saved registers (a12-a15), the return address (a0),
 *     the stack pointer (a1) inside xeros_ctx_save's frame, and special
 *     registers.  Caller-saved registers (a2-a11) are reconstructed by the
 *     window underflow when the task is resumed.
 *   - Calls xeros_window_spill to flush all live caller windows to the task
 *     stack before returning 0.
 *
 * xeros_ctx_restore_impl: call0 ABI (no window rotation)
 *   - Called from xeros_ctx_restore wrapper via call0.
 *   - For resumed tasks: restores a0, a1, a12-a15, special registers, PS and
 *     window state, places 1 in the return-value register, and executes retw.
 *     retw underflows from the saved xeros_ctx_save frame back to the caller.
 *   - For new tasks: loads all GPRs, sets PS for CALLINC=2 / WOE=1, and
 *     jumps to the trampoline via jx a0.
 *
 * xeros_ctx_restore: call8 ABI wrapper
 *   - Thin wrapper that calls xeros_ctx_restore_impl via call0
 *   - Never returns (ctx_restore_impl jumps away)
 *
 * Context structure layout (kern_ctx_native_t, 104 bytes total):
 *   Offset   0: a0        (kern_sched_tick's return addr to its caller)
 *   Offset   4: a1        (stack pointer)
 *   Offset   8: a2        (general purpose)
 *   Offset  12: a3        (general purpose)
 *   Offset  16: a4        (general purpose)
 *   Offset  20: a5        (general purpose)
 *   Offset  24: a6        (general purpose)
 *   Offset  28: a7        (general purpose)
 *   Offset  32: a8        (general purpose / cleanup handler for new tasks)
 *   Offset  36: a9        (general purpose / stack_top for new tasks)
 *   Offset  40: a10       (general purpose / pxCode for new tasks)
 *   Offset  44: a11       (general purpose / pvParameters for new tasks)
 *   Offset  48: a12       (general purpose)
 *   Offset  52: a13       (general purpose)
 *   Offset  56: a14       (general purpose)
 *   Offset  60: a15       (general purpose)
 *   Offset  64: SAR       (shift amount register)
 *   Offset  68: LBEG      (zero-overhead loop begin)
 *   Offset  72: LEND      (zero-overhead loop end)
 *   Offset  76: LCOUNT    (zero-overhead loop count)
 *   Offset  80: PS        (processor state)
 *   Offset  84: EXCCAUSE  (exception cause)
 *   Offset  88: EXCVADDR  (exception virtual address)
 *   Offset  92: PC        (saved program counter)
 *   Offset  96: WINDOWBASE  (window base register)
 *   Offset 100: WINDOWSTART (window start register)
 */

    .file   "ctx_switch.S"

/* External symbols */
    .extern kern_exit

/* ========================================================================== */
/*  Constants                                                                  */
/* ========================================================================== */

    .section .rodata
    .align  4

/**
 * Processor State value for new tasks (constructed at runtime by ctx_restore_impl):
 *   PS.WOE   = 1  (window overflow exceptions ENABLED)
 *   PS.CALLINC = 0  (jx does not set CALLINC; trampoline entry does NOT rotate)
 *   PS.UM    = 1  (user mode)
 *   PS.EXCM  = 0  (not in exception mode)
 *   PS.INTLEVEL = 0 (interrupts enabled so idle task waiti can wake)
 *   Bit 18 (WOE) = 1, Bit 5 (UM) = 1, Bit 3:0 = 0
 *   => 0x00040020 (before OR with current WB)
 *
 * CALLINC=0: We use `jx` to jump to the trampoline (not `call8`), so PS.CALLINC
 * is NOT set by the jump.  The trampoline's `entry sp, 16` reads PS.CALLINC=0
 * and does a normal frame allocation without window rotation.  The trampoline
 * then uses `call8 xeros_task_wrapper`, which sets CALLINC=2 for the wrapper's
 * entry instruction.
 *
 * INTLEVEL=0: Unlike FreeRTOS which uses `reti` to atomically restore PS
 * (masking interrupts during restore), we use `jx` which does not restore PS.
 * Interrupts must be enabled for the idle task's `waiti 0` to wake on timer.
 *
 * WINDOWSTART = 0x0001 lets the hardware overflow handler build the window
 * chain on the task stack.
 *
 * Window count: kern_sched(K) -> ctx_restore(K) -> trampoline(K via jx, no rotation)
 *   -> wrapper(K -> K+2 via call8) -> wrapper(K+2 -> K+4 via entry)
 *   -> task_entry(K+4 -> K+6 via call8) -> task_entry(K+6 -> K+8 via entry).
 *   All intermediate windows have valid SPs because each `entry` allocates
 *   stack space, so overflow handler save areas do not overlap.
 */
/* ========================================================================== */
/*  xeros_window_spill (private helper)                                        */
/* ========================================================================== */

    .section .iram1.text, "ax"
    .align  4
.L_xeros_wd_spill_assist28:
    entry   sp, 48
    call12  .L_xeros_wd_spill_assist16
    retw
    .align  4
.L_xeros_wd_spill_assist24:
    entry   sp, 32
    call8   .L_xeros_wd_spill_assist16
    retw
    .align  4
.L_xeros_wd_spill_assist20:
    entry   sp, 16
    call4   .L_xeros_wd_spill_assist16
    retw
    .align  4
.L_xeros_wd_spill_assist16:
    entry   sp, 16
    movi    a15, 0
    retw

    .section .text
    .align  4
    .global xeros_window_spill
    .type   xeros_window_spill, @function
xeros_window_spill:
    /* Spill all live caller register windows to the stack.
     * Works in non-privileged tasks (no xthal_window_spill_nw dependency).
     * On entry: a0 = encoded return address (from call8/call4/call12).
     * Allocates a 48-byte frame to allow call4/call8/call12. */
    entry   sp, 48
    bbci.l  a0, 31, 1f          /* branch if called with call4 */
    bbsi.l  a0, 30, 2f          /* branch if called with call12 */
    call8   .L_xeros_wd_spill_assist16  /* call8: need another 8 aregs */
    retw
1:
    call12  .L_xeros_wd_spill_assist16  /* call4: need another 12 aregs */
    retw
2:
    call4   .L_xeros_wd_spill_assist16  /* call12: need another 4 aregs */
    retw

    .size   xeros_window_spill, . - xeros_window_spill

/* ========================================================================== */
/*  Code Section                                                               */
/* ========================================================================== */

    .section .text
    .align  4

/* ========================================================================== */
/*  xeros_ctx_init                                                             */
/* ========================================================================== */

/**
 * @brief Initialize a new task context so it can be started by xeros_ctx_restore()
 *
 * call8 ABI: arguments arrive in a10-a15. After `entry sp, 32`:
 *   a2 = ctx, a3 = stack_base, a4 = stack_size, a5 = entry, a6 = arg
 *
 * New task context layout:
 *   ctx->a8  = &xeros_task_cleanup_handler  (→ a0 in ctx_restore, trampoline's retw return addr)
 *   ctx->a9  = aligned stack top            (→ a1 in ctx_restore, trampoline's SP)
 *   ctx->a10 = entry function               (loaded by trampoline into a10 → wrapper a2/a10)
 *   ctx->a11 = arg                          (loaded by trampoline into a11 → wrapper a3/a11)
 *   ctx->pc  = &xeros_task_trampoline       (marker to detect new vs resumed, jump target)
 *   ctx->ps  = 0x00060023                   (placeholder; ctx_restore_impl overwrites)
 *
 * Execution flow for new tasks (CALLINC=0, WOE=1):
 *   ctx_restore_impl constructs PS: CALLINC=0, WOE=1, UM=1, INTLEVEL=0
 *   ctx_restore_impl sets WINDOWSTART bit for current WB
 *   ctx_restore_impl loads: a0=cleanup, a1=stack_top, a2=pxCode, a3=pvParameters, a15=trampoline
 *   ctx_restore_impl: jx a15 (→ trampoline)
 *   trampoline: entry sp, 16 (CALLINC=0: no rotation, alloc 16-byte frame)
 *   trampoline: loads a10=pxCode, a11=pvParameters from ctx (a7, unchanged by entry)
 *   trampoline: call8 xeros_task_wrapper (CALLINC=2: rotate +2)
 *   wrapper: entry a1, N (CALLINC=2: rotate +2)
 *   wrapper: a2=pxCode, a3=pvParameters → callx8 a2 → pxCode(pvParameters)
 *   wrapper: kern_exit()
 *
 * Register mapping:
 *   ctx->a8  → ctx_restore a0 → trampoline a0 (no rotation) → wrapper a8 (cleanup/retw addr)
 *   ctx->a9  → ctx_restore a1 → trampoline a1 (no rotation) → wrapper a9 (stack_top/SP)
 *   ctx->a10 → trampoline loads a10 → wrapper a2/a10 (pxCode)
 *   ctx->a11 → trampoline loads a11 → wrapper a3/a11 (pvParameters)
 *   ctx->a15 → ctx_restore a15 → trampoline a15 (no rotation, jump target)
 */
    .global xeros_ctx_init
    .type   xeros_ctx_init, @function
    .align  4
xeros_ctx_init:
    entry   sp, 32                  /* call8: allocate 32-byte frame, rotate window */
    /* After entry: a2=ctx, a3=stack_base, a4=stack_size, a5=entry, a6=arg */

    /* Step 1: Compute 16-byte-aligned stack top */
    add     a3, a3, a4              /* a3 = stack_base + stack_size */
    movi    a4, 0xFFFFFFF0
    and     a3, a3, a4              /* a3 = aligned stack top */

    /* Step 1b: Initialize the bottom stack frame's previous-SP link.
     * Window overflow/underflow handlers read a1[-12] to find the next
     * outer stack frame. For a brand new task there is no outer frame;
     * point the link back to the stack top itself so the spill area
     * (a4-a7 at previous_SP - 32 .. -20) lands inside allocated stack.
     * Also initialize the next outer links for the trampoline and wrapper
     * frames so that window overflows do not read uninitialized memory.
     *
     * Execution path creates 3 window rotations (each can trigger overflow):
     *   1. trampoline entry sp, 16  → WB K→K+4,  a1 = stack_top - 16
     *   2. call8 wrapper            → WB K+4→K+8, a1 set by wrapper entry
     *   3. wrapper entry a1, N      → WB K+8→K+12, a1 = wrapper_a1 - N
     *
     * Each overflow handler reads a1[-12] from the NEW frame to find the
     * previous frame's SP. We pre-initialize links at 16-byte intervals
     * so that regardless of the wrapper's frame size N, a valid link exists.
     *   stack_top[-12]       → covers trampoline frame (a1 = stack_top - 16)
     *   (stack_top-16)[-12]  = stack_top[-28] → covers 16-byte wrapper frame
     *   (stack_top-32)[-12]  = stack_top[-44] → covers 32-byte wrapper frame
     *   (stack_top-48)[-12]  = stack_top[-60] → covers 48-byte wrapper frame
     *   (stack_top-64)[-12]  = stack_top[-76] → covers 64-byte wrapper frame */
    addi    a4, a3, -12             /* a4 = stack_top - 12 */
    s32i    a3, a4, 0               /* stack_top[-12] = stack_top */
    addi    a4, a3, -28             /* a4 = stack_top - 28 */
    s32i    a3, a4, 0               /* (stack_top - 16)[-12] = stack_top */
    addi    a4, a3, -44             /* a4 = stack_top - 44 */
    s32i    a3, a4, 0               /* (stack_top - 32)[-12] = stack_top */
    addi    a4, a3, -60             /* a4 = stack_top - 60 */
    s32i    a3, a4, 0               /* (stack_top - 48)[-12] = stack_top */
    addi    a4, a3, -76             /* a4 = stack_top - 76 */
    s32i    a3, a4, 0               /* (stack_top - 64)[-12] = stack_top */

    /* Step 2: Zero all registers in context */
    movi    a4, 0
    s32i    a4, a2, 0               /* ctx->a0  = 0 */
    s32i    a4, a2, 8               /* ctx->a2  = 0 */
    s32i    a4, a2, 12              /* ctx->a3  = 0 */
    s32i    a4, a2, 16              /* ctx->a4  = 0 */
    s32i    a4, a2, 20              /* ctx->a5  = 0 */
    s32i    a4, a2, 24              /* ctx->a6  = 0 */
    s32i    a4, a2, 28              /* ctx->a7  = 0 */
    s32i    a4, a2, 32              /* ctx->a8  = 0 (overwritten below) */
    s32i    a4, a2, 36              /* ctx->a9  = 0 (overwritten below) */
    s32i    a4, a2, 40              /* ctx->a10 = 0 (overwritten below) */
    s32i    a4, a2, 44              /* ctx->a11 = 0 (overwritten below) */
    s32i    a4, a2, 48              /* ctx->a12 = 0 */
    s32i    a4, a2, 52              /* ctx->a13 = 0 */
    s32i    a4, a2, 56              /* ctx->a14 = 0 */
    s32i    a4, a2, 60              /* ctx->a15 = 0 */
    s32i    a4, a2, 64              /* ctx->sar      = 0 */
    s32i    a4, a2, 68              /* ctx->lbeg     = 0 */
    s32i    a4, a2, 72              /* ctx->lend     = 0 */
    s32i    a4, a2, 76              /* ctx->lcount   = 0 */
    s32i    a4, a2, 84              /* ctx->exccause = 0 */
    s32i    a4, a2, 88              /* ctx->excvaddr = 0 */

    /* Step 3: Set register targets for CALLINC=2 trampoline + call8 wrapper
     *
     * ctx_restore_impl loads all GPR, then jumps to trampoline.
     * Trampoline's entry sp, 16 (CALLINC=2) rotates window +2.
     * After rotation, trampoline calls wrapper via call8.
     * call8 maps: trampoline a10→wrapper a10, a11→a11 (args preserved).
     * Wrapper receives pxCode in a10 and pvParameters in a11. */
    s32i    a3, a2, 4               /* ctx->a1 = stack_top (SP for old window spill during trampoline entry) */
    s32i    a3, a2, 20              /* ctx->a5 = stack_top (S32E save base: entry abort leaves a5 undefined) */
    movi    a7, xeros_task_cleanup_handler
    s32i    a7, a2, 32              /* ctx->a8 = cleanup handler (→ trampoline a0 = retw addr) */
    s32i    a3, a2, 36              /* ctx->a9 = stack_top (→ trampoline a1 = SP) */
    s32i    a5, a2, 40              /* ctx->a10 = entry function (→ wrapper a10 = pxCode) */
    s32i    a6, a2, 44              /* ctx->a11 = arg (→ wrapper a11 = pvParameters) */

    /* Step 4: Set trampoline as PC (ctx_restore_impl uses this to detect new tasks) */
    movi    a7, xeros_task_trampoline
    s32i    a7, a2, 92              /* ctx->pc = &xeros_task_trampoline (detection marker) */

    /* Step 5: Set processor state (placeholder for resumed tasks).
     * For NEW tasks, ctx_restore_impl constructs PS from the current register,
     * so this value is NOT used.  For resumed tasks, ctx_save captures the
     * real PS (CALLINC=2 from entry, INTLEVEL from the task). */
    movi    a3, 0x00060023
    s32i    a3, a2, 80              /* ctx->ps = placeholder */

    /* Step 6: Set window registers for new task.
     * windowbase is initialized to 0; ctx_restore_impl will keep the current
     * scheduler WB for new tasks. windowstart = 0x0001 marks only the current
     * window valid, so the trampoline's entry/call instructions naturally
     * trigger window overflow exceptions and build a valid SP chain on the
     * task stack. The bottom frame's a1[-12] was initialized to stack_top in
     * Step 1b, giving the overflow handler a valid spill area. */
    movi    a3, 0
    s32i    a3, a2, 96              /* ctx->windowbase = 0 (placeholder, overridden by scheduler WB) */
    movi    a3, 0x0001
    s32i    a3, a2, 100             /* ctx->windowstart = 0x0001 (only current window valid) */

    retw                            /* return (call8 windowed return) */

    .size   xeros_ctx_init, . - xeros_ctx_init

/* ========================================================================== */
/*  xeros_task_trampoline                                                      */
/* ========================================================================== */

/**
 * @brief Trampoline for newly created tasks
 *
 * Reached via jx a15 from ctx_restore_impl. PS.CALLINC=2, WOE=1.
 * WINDOWSTART has only the scheduler's window bit set; window overflow
 * exceptions handle saving/restoring as new windows are created.
 *
 * Before entry (WB = K):
 *   a0  = cleanup handler  (return address for wrapper's retw)
 *   a1  = stack_top        (SP for entry allocation)
 *   a2  = pxCode           (loaded by ctx_restore_impl from ctx->a10)
 *   a3  = pvParameters     (loaded by ctx_restore_impl from ctx->a11)
 *
 * After trampoline's `entry sp, 16` (CALLINC=2, +2 rotation, WB = K+2):
 *   a0 = old a8  = cleanup handler (wrapper's retw return addr)
 *   a1 = old a9  = stack_top - 16 (SP)
 *   a10 = old a2 = pxCode
 *   a11 = old a3 = pvParameters
 *
 * After call8 xeros_task_wrapper (CALLINC=2, +2 rotation, WB = K+4):
 *   wrapper a2  = trampoline a10 = pxCode       (callx8 target after wrapper entry)
 *   wrapper a3  = trampoline a11 = pvParameters  (callx8 arg after wrapper entry)
 *   wrapper a10 = trampoline a2  = pxCode       (call8 arg0, via entry rotation)
 *   wrapper a11 = trampoline a3  = pvParameters (call8 arg1, via entry rotation)
 *   wrapper a0  = encoded return address (→ cleanup handler after retw)
 *   wrapper a1  = trampoline SP
 *
 * Wrapper calls pxCode(pvParameters), then kern_exit().
 * If pxCode returns, wrapper's retw returns to cleanup handler.
 */
    .section .iram1.text, "ax"
    .global xeros_task_trampoline
    .type   xeros_task_trampoline, @function
    .align  4
    .global g_trampoline_reached
    .section .data
    .align  4
g_trampoline_reached:
    .word   0
    .section .iram1.text, "ax"

xeros_task_trampoline:
    /* Reached via jx a5 from ctx_restore_impl. PS.CALLINC=2, WOE=1,
     * INTLEVEL=3 (interrupts masked during restore).
     * Before entry (WB = K): a0 = cleanup handler, a1 = stack_top,
     *   a2 = pxCode, a3 = pvParameters.
     *
     * entry sp, 16 with CALLINC=2:
     *   - Rotates window +2 (WB: K → K+2)
     *   - Allocates 16 bytes on stack (SP -= 16)
     *   - After rotation:
     *     a8  = old a0  = cleanup handler (→ wrapper a0 via call8)
     *     a9  = old a1  = stack_top       (→ wrapper a1 via call8)
     *     a10 = old a2  = pxCode          (→ wrapper a2 via call8)
     *     a11 = old a3  = pvParameters    (→ wrapper a3 via call8)
     *
     * call8 xeros_task_wrapper (CALLINC=2, +2 rotation):
     *   wrapper a0  = trampoline a8  = cleanup handler
     *   wrapper a1  = trampoline a9  = stack_top
     *   wrapper a2  = trampoline a10 = pxCode
     *   wrapper a3  = trampoline a11 = pvParameters
     */
    entry   sp, 16                  /* CALLINC=2: rotate +2, alloc 16-byte frame */

    /* DEBUG: write to g_trampoline_reached to verify trampoline executes */
    movi    a4, g_trampoline_reached
    movi    a5, 1
    s32i    a5, a4, 0

    /* INTLEVEL remains at 3 (set by ctx_restore_impl). This is INTENTIONAL:
     * the GPTimer ISR fires in FreeRTOS interrupt context, which tries to
     * save/restore Xtensa window registers. When a Xeros task is running,
     * its window state (WINDOWBASE/WINDOWSTART) differs from what FreeRTOS
     * expects, causing a crash in _xt_context_save.
     *
     * Instead of hardware interrupts, the idle loop polls the GPTimer flag
     * and calls kern_sched_tick() cooperatively. Tasks yield via kern_yield(),
     * returning control to the scheduler and eventually to the idle loop. */

    /* a10 = pxCode, a11 = pvParameters (from entry rotation of a2/a3). */
    call8   xeros_task_wrapper      /* CALLINC=2: rotate +2, never returns normally */

    .size   xeros_task_trampoline, . - xeros_task_trampoline

/* ========================================================================== */
/*  xeros_task_cleanup_handler                                                 */
/* ========================================================================== */

/**
 * @brief Cleanup handler for tasks that return from their entry function
 *
 * When xeros_task_wrapper returns via retw, execution lands here
 * (the instruction after call8 in the trampoline).
 *
 * Window count at this point: trampoline(K+4). We use call0 (NOT callx8)
 * to call xeros_ctx_restore_impl, which does NOT rotate the window.
 * This avoids consuming additional window levels.
 */
    .section .iram1.text, "ax"
    .global xeros_task_cleanup_handler
    .type   xeros_task_cleanup_handler, @function
    /* No .align 4 here -- must be contiguous with trampoline for call8 fall-through */
xeros_task_cleanup_handler:
    /* Directly restore scheduler context without creating a new window.
     * a2 = &g_sched_ctx (loaded by l32r from literal pool).
     * xeros_ctx_restore_impl uses call0 ABI: first arg in a2, no entry. */
    movi    a2, g_sched_ctx         /* a2 = &g_sched_ctx; assembler uses l32r internally */
    call0   xeros_ctx_restore_impl  /* restore scheduler context -- never returns */

.L_cleanup_halt:
    j       .L_cleanup_halt          /* infinite loop: should never reach here */

    .size   xeros_task_cleanup_handler, . - xeros_task_cleanup_handler

/* ========================================================================== */
/*  xeros_ctx_save                                                             */
/* ========================================================================== */

/**
 * @brief Save the current execution context (setjmp-like semantics)
 *
 * Standard call8 ABI function with `entry` / `retw`.
 * Called from C via call8; the first argument (ctx) is in a10 before
 * `entry`, and in a2 after `entry`.
 *
 * We follow the Xtensa setjmp model: only the callee-saved registers
 * (a12-a15), the stack pointer (a1), the return address (a0), and the
 * special registers need to be saved.  The caller-saved registers
 * (a2-a11) are not live across the call and are reconstructed by the
 * window underflow when the context is resumed.
 *
 * The saved a1 is the stack pointer *inside* xeros_ctx_save's own frame,
 * so that a later retw can underflow and restore the caller's registers
 * from the stack.
 *
 * Returns 0 on the save path. When restored via xeros_ctx_restore(),
 * execution resumes at the instruction after the original call8 and
 * returns 1.
 *
 * @param a10 ctx (before entry) / a2 ctx (after entry)
 * @return 0 (save path) or 1 (restore path)
 */
    .global xeros_ctx_save
    .type   xeros_ctx_save, @function
    .align  4
xeros_ctx_save:
    /* Before entry: a10 = ctx, a0 = encoded return address, a1 = caller's SP.
     * entry allocates our frame and rotates the window; after entry a2 = ctx. */
    entry   sp, 48

    /* Save the return address, our own SP, and the callee-saved registers.
     * After entry, a2 = ctx; a0 still holds the encoded return address;
     * a1 is now xeros_ctx_save's own stack pointer. */
    s32i    a0,  a2, 0              /* ctx->a0: encoded return address */
    s32i    a1,  a2, 4              /* ctx->a1: our SP (for underflow on resume) */
    s32i    a12, a2, 48             /* ctx->a12: callee-saved */
    s32i    a13, a2, 52             /* ctx->a13: callee-saved */
    s32i    a14, a2, 56             /* ctx->a14: callee-saved */
    s32i    a15, a2, 60             /* ctx->a15: callee-saved */

    /* Spill all live caller windows to their stacks.  This ensures that the
     * caller's caller-saved registers are saved on the task stack and can be
     * recovered by window underflow when the task resumes. */
    call8   xeros_window_spill

    /* Save special registers (a3 used as scratch). */
    rsr     a3, SAR;        s32i a3, a2, 64
    rsr     a3, LBEG;       s32i a3, a2, 68
    rsr     a3, LEND;       s32i a3, a2, 72
    rsr     a3, LCOUNT;     s32i a3, a2, 76
    rsr     a3, PS;         s32i a3, a2, 80
    rsr     a3, EXCCAUSE;   s32i a3, a2, 84
    rsr     a3, EXCVADDR;   s32i a3, a2, 88

    /* Save the return address as ctx->pc (raw, with CALLINC/OWB bits).
     * ctx_restore_impl compares only bits [31:8] against xeros_task_trampoline
     * to detect new vs resumed tasks, ignoring the CALLINC and OWB bits. */
    l32i    a3, a2, 0               /* a3 = raw encoded return address */
    s32i    a3, a2, 92              /* ctx->pc = raw encoded address */

    /* Save window state (our window, after spill). */
    rsr     a3, WINDOWBASE;  s32i a3, a2, 96
    rsr     a3, WINDOWSTART; s32i a3, a2, 100

    /* Return value 0 goes in a2; after retw it appears in the caller's a10. */
    l32i    a0,  a2, 0              /* restore encoded return address */
    l32i    a1,  a2, 4              /* restore our SP */
    movi    a2, 0

    retw                            /* rotate window back and return to caller */

    .size   xeros_ctx_save, . - xeros_ctx_save

/* ========================================================================== */
/*  xeros_ctx_restore                                                          */
/* ========================================================================== */

/**
 * @brief Restore a previously saved execution context (call8 wrapper)
 *
 * call8 ABI: called from C, so a10 = ctx before entry, a2 = ctx after entry.
 * MUST have entry instruction: creates our own stack frame so that retw
 * in ctx_restore_impl has valid underflow save areas to restore the caller's
 * registers from. Without entry, the underflow handler reads from the
 * caller's frame which may not have the correct save area contents.
 *
 * @param a10 ctx  (call8 ABI: first arg in a10)
 */
    .global xeros_ctx_restore
    .type   xeros_ctx_restore, @function
    .align  4
xeros_ctx_restore:
    entry   sp, 16                  /* allocate our own frame (CALLINC=2) */
    /* After entry with CALLINC=2, window rotates by 8 registers:
     *   new a0 = old a8 (OVERWRITTEN by entry with encoded return info)
     *   new a2 = old a10 = ctx   (the argument from call8)
     *
     * entry clobbers a0, so ctx is in a2, NOT a0.
     * ctx_restore_impl expects ctx in a2 (call0 ABI). */
    l32i    a3, a2, 0               /* a3 = ctx->a0 (encoded return address) */
    mov     a0, a3                  /* a0 = encoded return address (for retw) */
    call0   xeros_ctx_restore_impl  /* never returns */

    .size   xeros_ctx_restore, . - xeros_ctx_restore

/* ========================================================================== */
/*  xeros_ctx_restore_impl                                                     */
/* ========================================================================== */

/**
 * @brief Restore a previously saved execution context (call0 implementation)
 *
 * call0 ABI (no window rotation):
 *   - Arguments in a2-a7 (same register window as caller)
 *   - No `entry` instruction
 *   - Never returns to its direct caller (resumes the target context)
 *
 * Restores all special registers and general-purpose registers from the
 * context structure, then resumes the target context.
 *
 * For RESUMED tasks: ctx->ps and ctx->windowbase/windowstart are restored to
 *   the exact values captured by xeros_ctx_save. The encoded return address
 *   stored in ctx->a0 is loaded into a0 and retw is used: it rotates the
 *   saved task window back to the caller (kern_sched_tick) and resumes at
 *   the instruction following the original call8 xeros_ctx_save.
 *
 * For NEW tasks: PS is constructed at runtime: CALLINC=0, WOE=1, UM=1, INTLEVEL=0.
 *   WINDOWSTART has only the scheduler's window bit set.
 *   After jx a15, execution begins at the trampoline (= ctx->pc).
 *   ctx_restore_impl sets: a0 = cleanup handler, a1 = stack_top,
 *   a2 = pxCode, a3 = pvParameters (loaded last since a2 = ctx is clobbered).
 *   The trampoline's `entry sp, 16` (CALLINC=0) allocates frame WITHOUT rotation.
 *   Trampoline loads a10/a11 from ctx (a7) and calls wrapper via call8.
 *   call8 sets CALLINC=2; wrapper's `entry a1, N` rotates +2.
 *   After wrapper entry: a2 = pxCode, a3 = pvParameters (via window rotation).
 *   Wrapper calls pxCode(pvParameters) via callx8 a2.
 *
 * @param a2 ctx  (call0 ABI: first arg in a2)
 */
    .global xeros_ctx_restore_impl
    .type   xeros_ctx_restore_impl, @function
    .align  4
xeros_ctx_restore_impl:
    /* No entry instruction -- we stay in the caller's window. */

    /* ------------------------------------------------------------------ */
    /* Step 1: Interrupt masking                                          */
    /*   We do NOT use rsil here — it is a privileged instruction and    */
    /*   the scheduler may run in user mode (PS.UM=1) under FreeRTOS.   */
    /*   The PS write below sets INTLEVEL=3, which masks interrupts      */
    /*   for the duration of the context switch.                         */
    /*                                                                    */
    /*   NOTE: We do NOT call xthal_window_spill_nw here.                */
    /*   That function assumes a9 register chain is set up by normal     */
    /*   function calls, but our windows were created by ctx_save which  */
    /*   doesn't establish the a9 chain. Calling it would corrupt memory. */
    /*   Instead, we ensure the old window's SP (ctx->a1) is valid so    */
    /*   that the trampoline's `entry` instruction can trigger a window  */
    /*   overflow exception that correctly spills the old window.         */
    /* ------------------------------------------------------------------ */

    /* ------------------------------------------------------------------ */
    /* Step 3: Restore special registers from context                     */
    /*   a2 = ctx (call0 arg register). a3 used as scratch.              */
    /* ------------------------------------------------------------------ */
    l32i    a3, a2, 64              /* a3 = ctx->sar */
    wsr     a3, SAR

    /* Skip LBEG/LEND/LCOUNT restore */
    /* l32i    a3, a2, 76              a3 = ctx->lcount */
    /* wsr     a3, LCOUNT */

    /* ------------------------------------------------------------------ */
    /* Step 2b: Restore window registers from context                     */
    /*   Must be done BEFORE PS restore (PS.OWB is a shadow used by       */
    /*   exception handlers; the real WINDOWBASE controls which physical   */
    /*   registers are visible as a0-a15).                                 */
    /*                                                                     */
    /*   NEW tasks: keep the current scheduler's WINDOWBASE; set all       */
    /*   WINDOWSTART bits valid so the trampoline chain (entry + call8     */
    /*   rotations) doesn't trigger WindowUnderflow exceptions.            */
    /*                                                                     */
    /*   RESUMED tasks: use saved WINDOWSTART (already 0xFFFF from         */
    /*   ctx_save's post-spill fixup).                                     */
    /* ------------------------------------------------------------------ */

    l32i    a4, a2, 92              /* a4 = ctx->pc (raw, with CALLINC/OWB) */
    srli    a4, a4, 8               /* compare only bits [31:8] */
    movi    a5, xeros_task_trampoline
    srli    a5, a5, 8
    bne     a4, a5, .L_ws_resumed   /* if resumed task, restore saved window regs */

    /* New task: keep current WINDOWBASE, mark ALL windows valid in WINDOWSTART.
     * The trampoline chain does entry(+2) then call8(+2), touching multiple
     * windows. With only the scheduler's bit set, each rotation triggers a
     * WindowUnderflow that reads uninitialized stack data → Double exception.
     * Setting all bits (0xFF for 8 windows) prevents this. */
    movi    a3, 0xFF                /* all 8 window bits valid */
    wsr     a3, WINDOWSTART
    rsync

    j       .L_ws_done

.L_ws_resumed:
    /* DO NOT restore WINDOWBASE here — writing WINDOWBASE shifts the register
     * window, making subsequent l32i instructions write to WRONG physical
     * registers. Instead, rely on retw to restore WINDOWBASE from the OWB
     * encoded in ctx->a0 (the saved return address from ctx_save).
     *
     * WINDOWSTART: ctx_save captured it AFTER the spill, so it only has the
     * scheduler's window bit set. We must OR in the task's window bit
     * (from ctx->windowbase) so that retw rotating to the task's window
     * doesn't trigger a WindowUnderflow. */
    l32i    a3, a2, 100             /* a3 = ctx->windowstart */
    l32i    a4, a2, 96              /* a4 = ctx->windowbase */
    movi    a5, 1
    ssl     a4                      /* SAR = windowbase */
    sll     a4, a5                  /* a4 = 1 << windowbase */
    or      a3, a3, a4              /* a3 = windowstart | (1 << windowbase) */
    wsr     a3, WINDOWSTART
    rsync

.L_ws_done:

    /* Restore PS: take CALLINC/WOE/UM/etc from ctx->ps, but KEEP the current
     * Window Base (WB, bits [11:8]) for NEW tasks.
     *
     * For NEW tasks: set CALLINC=2 (trampoline uses call8 to wrapper), WOE=1
     *   (window overflow exceptions enabled for rotation), UM=1 (user mode),
     *   INTLEVEL=3 (mask interrupts during restore).
     *   ctx_restore_impl jumps to trampoline via jx a5 (= ctx->pc).
     *   Trampoline's `entry sp, 16` (CALLINC=2) rotates window +2.
     *
     * For RESUMED tasks: restore PS fully from ctx->ps (includes the correct
     *   WB from when the task was saved). */
    l32i    a4, a2, 80              /* a4 = ctx->ps */
    l32i    a5, a2, 92              /* a5 = ctx->pc (raw) */
    srli    a5, a5, 8               /* compare bits [31:8] only */
    movi    a6, xeros_task_trampoline
    srli    a6, a6, 8
    bne     a5, a6, .L_ps_resumed   /* if resumed task, use full ctx->ps */

    /* New task: CALLINC=2, WOE=1, UM=1, INTLEVEL=3.
     *
     * CALLINC=2: The trampoline's `entry sp, 16` reads PS.CALLINC to determine
     * window rotation.  With CALLINC=2, entry rotates +2, which puts the
     * cleanup handler (old a0) into trampoline a8 and stack_top (old a1) into
     * trampoline a9.  After call8 wrapper (+2 rotation), wrapper a0 = cleanup
     * handler and wrapper a1 = stack_top, which is exactly what we need.
     *
     * INTLEVEL=3: Interrupts must be masked during context restore to prevent
     * the GPTimer ISR from re-entering kern_sched_tick mid-restore.
     *
     * Keep current WB (bits [11:8]) — trampoline will rotate from here. */
    rsr     a3, PS                  /* a3 = current PS (has correct WB) */
    movi    a4, 0xF00               /* WB mask: bits [11:8] */
    and     a3, a3, a4              /* a3 = current WB only */
    movi    a4, 0x60023             /* CALLINC=2, WOE=1, UM=1, INTLEVEL=3 */
    or      a3, a3, a4              /* WB | CALLINC=2 | WOE=1 | UM=1 | INTLEVEL=3 */
    j       .L_ps_write

.L_ps_resumed:
    /* Resumed task: use full ctx->ps. This includes OWB = K_orig (the window
     * base when ctx_save was called). We do NOT change WINDOWBASE here to
     * avoid corrupting the register mapping. The OWB mismatch with the
     * current WINDOWBASE is temporary and harmless — retw will set
     * WINDOWBASE = OWB when it executes, restoring the correct value. */
    mov     a3, a4                  /* a3 = ctx->ps (full restore) */

.L_ps_write:
    wsr     a3, PS
    rsync                           /* PS takes effect */

    /* Padding: ensure instruction alignment after PS restore */
    nop
    nop

    /* ------------------------------------------------------------------ */
    /* Step 3: Restore general-purpose registers from context             */
    /*   a2 is the base pointer. Must load a0 before a2 itself.          */
    /*   For new tasks: jump to trampoline via jx a0.                    */
    /*   For resumed tasks: a10 = 1, retw using ctx->a0.                 */
    /*   We must decide BEFORE a2 is overwritten (it's the last load).   */
    /* ------------------------------------------------------------------ */

    l32i    a3, a2, 92              /* a3 = ctx->pc (raw) */
    srli    a3, a3, 8               /* compare bits [31:8] only */
    movi    a4, xeros_task_trampoline
    srli    a4, a4, 8
    beq     a3, a4, .L_new_task     /* if upper bits match trampoline, new task */

    /* Resumed task: restore the callee-saved registers, the stack pointer,
     * and the encoded return address.  The caller-saved registers (a2-a11)
     * are not live across xeros_ctx_save(); they are reconstructed by the
     * window underflow triggered when retw returns to the caller.
     *
     * The saved a1 is xeros_ctx_save's own SP, so retw underflows from that
     * frame and restores the caller's a0-a7 from its base/extra save areas. */
    l32i    a1,  a2, 4              /* a1 = saved SP (inside xeros_ctx_save frame) */
    l32i    a12, a2, 48             /* restore callee-saved registers */
    l32i    a13, a2, 52
    l32i    a14, a2, 56
    l32i    a15, a2, 60
    l32i    a0,  a2, 0              /* a0 = encoded return address */
    movi    a2, 1                   /* return value for caller's a10 */

    retw                            /* underflow/return to caller */

.L_new_task:
    /* New task: load all GPRs and jump to trampoline.
     *
     * Register setup BEFORE jx a15 (WB = K):
     *   a0  = cleanup handler  (after entry: trampoline a8, then wrapper a0)
     *   a1  = stack_top        (after entry: trampoline a9, then wrapper a1)
     *   a2  = pxCode           (after entry: trampoline a10, then wrapper a2)
     *   a3  = pvParameters     (after entry: trampoline a11, then wrapper a3)
     *   a15 = trampoline addr  (jump target)
     *
     * Trampoline entry (CALLINC=2) rotates +2, putting a0→a8, a1→a9,
     * a2→a10, a3→a11.  Then call8 wrapper (+2) maps:
     *   trampoline a8→wrapper a0, a9→a1, a10→a2, a11→a3.
     *   Wrapper's entry (CALLINC=2) rotates +2: a10→a2 (pxCode), a11→a3 (pvParameters).
     *   Wrapper calls pxCode(pvParameters), then kern_exit(). */
    l32i    a0, a2, 32              /* a0 = ctx->a8 = cleanup handler */
    l32i    a1, a2, 4               /* a1 = ctx->a1 = stack_top */
    l32i    a15, a2, 92             /* a15 = ctx->pc = trampoline (jump target) */
    /* Load pxCode/pvParameters LAST since overwriting a2 loses ctx ptr. */
    l32i    a3, a2, 44              /* a3 = ctx->a11 = pvParameters (→ wrapper a3/a11) */
    l32i    a2, a2, 40              /* a2 = ctx->a10 = pxCode (→ wrapper a2/a10) — overwrites ctx ptr */

    jx      a15                     /* jump to trampoline; a0 = cleanup, a1 = stack_top */

    .size   xeros_ctx_restore_impl, . - xeros_ctx_restore_impl

#endif /* !NATIVE_TEST && !XEROS_NATIVE_SCHED */
