/* Only compile for Xtensa targets (ESP32), skip on native test builds */
#ifndef NATIVE_TEST

/**
 * @file ctx_switch.S
 * @brief Xeros context switch engine -- Xtensa call8 ABI implementation for ESP32
 *
 * This file contains the lowest-level context switch primitives for the Xeros
 * microkernel running on ESP32 (Xtensa LX6). All functions use the call8 ABI
 * (windowed calling convention), meaning arguments are passed in a10-a15 and
 * the register window rotates on function entry via `entry sp, 32`.
 *
 * Context structure layout (kern_ctx_native_t, 96 bytes total):
 *   Offset  0: a0        (return address)
 *   Offset  4: a1        (stack pointer)
 *   Offset  8: a2        (general purpose)
 *   Offset 12: a3        (general purpose)
 *   Offset 16: a4        (general purpose)
 *   Offset 20: a5        (general purpose / task entry for new tasks)
 *   Offset 24: a6        (general purpose / task arg for new tasks)
 *   Offset 28: a7        (general purpose)
 *   Offset 32: a8        (general purpose)
 *   Offset 36: a9        (general purpose)
 *   Offset 40: a10       (general purpose)
 *   Offset 44: a11       (general purpose)
 *   Offset 48: a12       (general purpose)
 *   Offset 52: a13       (general purpose)
 *   Offset 56: a14       (general purpose)
 *   Offset 60: a15       (general purpose)
 *   Offset 64: SAR       (shift amount register)
 *   Offset 68: LBEG      (zero-overhead loop begin)
 *   Offset 72: LEND      (zero-overhead loop end)
 *   Offset 76: LCOUNT    (zero-overhead loop count)
 *   Offset 80: PS        (processor state)
 *   Offset 84: EXCCAUSE  (exception cause)
 *   Offset 88: EXCVADDR  (exception virtual address)
 *   Offset 92: PC        (saved program counter)
 *
 * call8 ABI note:
 *   The ESP32 Xtensa GCC compiler uses call8 (windowed) ABI by default.
 *   When a function is called, arguments arrive in a10-a15. The `entry sp, 32`
 *   instruction rotates the register window so that a10→a2, a11→a3, etc.
 *   After entry, the callee sees arguments in a2-a7.
 */

    .file   "ctx_switch.S"

/* ========================================================================== */
/*  Constants                                                                  */
/* ========================================================================== */

    .section .rodata
    .align  4

/**
 * Processor State value for new tasks.
 *   PS.WOE   = 1  (window overflow exceptions ENABLED)
 *   PS.CALLINC = 0  (trampoline uses callx8 which sets CALLINC automatically)
 *   PS.OWB   = 0  (overflow window base = 0)
 *   PS.UM    = 1  (user mode)
 *   PS.EXCM  = 0  (not in exception mode)
 *   PS.INTLEVEL = 0 (interrupts enabled)
 *   Bit 18 (WOE) = 1, Bit 5 (UM) = 1 => 0x00040020
 *
 * The trampoline uses `callx8 a5` which sets CALLINC=2 and preserves WOE.
 * The callee's `entry sp, 32` may trigger a window overflow; WOE=1 ensures
 * the CPU traps to the overflow handler instead of IllegalInstruction.
 */
    .global xeros_ps_init
    .type   xeros_ps_init, @object
    .align  4
xeros_ps_init:
    .word   0x00040020
    .size   xeros_ps_init, . - xeros_ps_init

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
 * Sets up the context with a properly aligned stack pointer, entry point PC,
 * and a trampoline return address. When first restored, execution begins at
 * the entry function with arg passed in a2 (call8 ABI first argument).
 *
 * call8 ABI: arguments arrive in a10-a15. After `entry sp, 32`:
 *   a2 = ctx, a3 = stack_base, a4 = stack_size, a5 = entry, a6 = arg
 *
 * New task context layout:
 *   ctx->a0  = xeros_task_trampoline  (return address when entry returns)
 *   ctx->a1  = aligned stack top
 *   ctx->a2  = 0  (cleared)
 *   ctx->a5  = entry  (task entry function)
 *   ctx->a6  = arg    (task argument)
 *   ctx->pc  = xeros_task_trampoline  (first instruction to execute)
 *   ctx->ps  = 0x00020020 (user mode, interrupts enabled, CALLINC=2 for call8)
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

    /* Step 2: Zero all registers in context */
    movi    a4, 0
    s32i    a4, a2, 0               /* ctx->a0  = 0 */
    s32i    a4, a2, 4               /* ctx->a1  = 0 */
    s32i    a4, a2, 8               /* ctx->a2  = 0 */
    s32i    a4, a2, 12              /* ctx->a3  = 0 */
    s32i    a4, a2, 16              /* ctx->a4  = 0 */
    s32i    a4, a2, 20              /* ctx->a5  = 0 (will be overwritten below) */
    s32i    a4, a2, 24              /* ctx->a6  = 0 (will be overwritten below) */
    s32i    a4, a2, 28              /* ctx->a7  = 0 */
    s32i    a4, a2, 32              /* ctx->a8  = 0 */
    s32i    a4, a2, 36              /* ctx->a9  = 0 */
    s32i    a4, a2, 40              /* ctx->a10 = 0 */
    s32i    a4, a2, 44              /* ctx->a11 = 0 */
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

    /* Step 3: Set stack pointer (ctx->a1) */
    s32i    a3, a2, 4               /* ctx->a1 = stack_top */

    /* Step 4: Set return address to trampoline (ctx->a0) */
    movi    a7, xeros_task_trampoline
    s32i    a7, a2, 0               /* ctx->a0 = &xeros_task_trampoline */

    /* Step 5: Set entry point and arg for the trampoline
     * After ctx_restore, a5=entry and a6=arg are loaded from context.
     * The trampoline's `entry sp, 32` rotates window: a5→a13, a6→a14.
     * Then `call8 a13` invokes entry(arg) with arg in a10. */
    s32i    a5, a2, 20              /* ctx->a5 = entry */
    s32i    a6, a2, 24              /* ctx->a6 = arg */

    /* Step 6: Set program counter to trampoline (ctx->pc)
     * xeros_ctx_restore loads a0 from ctx->pc and jumps to it.
     * The trampoline will call entry(arg) via call8. */
    s32i    a7, a2, 92              /* ctx->pc = &xeros_task_trampoline */

    /* Step 7: Set processor state (PS = 0x20, user mode, interrupts on) */
    movi    a3, xeros_ps_init
    l32i    a3, a3, 0
    s32i    a3, a2, 80              /* ctx->ps = 0x00000020 */

    retw                            /* return (call8 windowed return) */

    .size   xeros_ctx_init, . - xeros_ctx_init

/* ========================================================================== */
/*  xeros_task_trampoline                                                      */
/* ========================================================================== */

/**
 * @brief Trampoline for newly created tasks
 *
 * When a newly created task is first restored by xeros_ctx_restore(), execution
 * begins here (ctx->pc = trampoline). At this point:
 *   a5 = entry, a6 = arg (loaded from context by ctx_restore)
 *
 * The trampoline does NOT use `entry sp, N` to avoid triggering window overflow
 * exceptions on the freshly-initialized context (whose old register window
 * values are all zero, causing the overflow handler to save to invalid memory).
 *
 * Instead, the trampoline:
 *   1. Moves arg into a10 (call8 first argument register)
 *   2. Uses `callx8 a5` to call entry(arg)
 *      - callx8 sets CALLINC=2 in PS and rotates the window
 *      - The callee's `entry sp, 32` sees CALLINC=2 and works correctly
 *   3. Calls kern_exit(0) when entry returns
 *
 * NOTE: Placed in IRAM (.iram1.text) to avoid flash instruction cache
 * coherency issues — flash may not be synced after OTA/upload.
 */
    .section .iram1.text, "ax"
    .global xeros_task_trampoline
    .type   xeros_task_trampoline, @function
    .align  4
xeros_task_trampoline:
    /* No entry instruction — we're reached via jx a0 (not call8), so
     * there's no caller window to unwind. callx8 below sets CALLINC=2
     * and rotates the window for the callee. */
    mov     a10, a6                 /* a10 = arg (call8 first argument) */
    callx8  a5                      /* entry(arg) -- callx8 sets CALLINC=2 */
    movi    a10, 0                  /* a10 = 0 (exit status) */
    call8   kern_exit               /* kern_exit(0) -- does not return */

.L_trampoline_halt:
    j       .L_trampoline_halt      /* infinite loop: should never reach here */

    .size   xeros_task_trampoline, . - xeros_task_trampoline

/* ========================================================================== */
/*  xeros_ctx_save                                                             */
/* ========================================================================== */

/**
 * @brief Save the current execution context (setjmp-like semantics)
 *
 * call8 ABI: After `entry sp, 32`, a2 = ctx (was a10 before rotation).
 *
 * Saves all general-purpose registers (a0-a15), special registers
 * (SAR, LBEG, LEND, LCOUNT, PS, EXCCAUSE, EXCVADDR), and the return
 * address (as PC) into the context structure.
 *
 * Returns 0 on the save path (first call). When later restored via
 * xeros_ctx_restore(), execution resumes here and returns 1.
 *
 * @param a10 ctx  (call8 ABI: first arg in a10)
 * @return 0 (save path) or 1 (restore path)
 */
    .global xeros_ctx_save
    .type   xeros_ctx_save, @function
    .align  4
xeros_ctx_save:
    entry   sp, 32                  /* call8: allocate frame, rotate window */
    /* After entry: a2 = ctx (was a10 before rotation) */

    /* ------------------------------------------------------------------ */
    /* Save all 16 general-purpose registers                              */
    /*   a0-a15 are stored at offsets 0-60 in the context structure.      */
    /* ------------------------------------------------------------------ */
    s32i    a0, a2, 0               /* ctx->a0  (return address) */
    s32i    a1, a2, 4               /* ctx->a1  (stack pointer) */
    s32i    a2, a2, 8               /* ctx->a2  (ctx pointer -- saved as-is) */
    s32i    a3, a2, 12              /* ctx->a3 */
    s32i    a4, a2, 16              /* ctx->a4 */
    s32i    a5, a2, 20              /* ctx->a5 */
    s32i    a6, a2, 24              /* ctx->a6 */
    s32i    a7, a2, 28              /* ctx->a7 */
    s32i    a8, a2, 32              /* ctx->a8 */
    s32i    a9, a2, 36              /* ctx->a9 */
    s32i    a10, a2, 40             /* ctx->a10 */
    s32i    a11, a2, 44             /* ctx->a11 */
    s32i    a12, a2, 48             /* ctx->a12 */
    s32i    a13, a2, 52             /* ctx->a13 */
    s32i    a14, a2, 56             /* ctx->a14 */
    s32i    a15, a2, 60             /* ctx->a15 */

    /* ------------------------------------------------------------------ */
    /* Save special registers                                             */
    /* ------------------------------------------------------------------ */
    rsr     a3, SAR
    s32i    a3, a2, 64              /* ctx->sar */

    rsr     a3, LBEG
    s32i    a3, a2, 68              /* ctx->lbeg */

    rsr     a3, LEND
    s32i    a3, a2, 72              /* ctx->lend */

    rsr     a3, LCOUNT
    s32i    a3, a2, 76              /* ctx->lcount */

    rsr     a3, PS
    s32i    a3, a2, 80              /* ctx->ps */

    rsr     a3, EXCCAUSE
    s32i    a3, a2, 84              /* ctx->exccause */

    rsr     a3, EXCVADDR
    s32i    a3, a2, 88              /* ctx->excvaddr */

    /* ------------------------------------------------------------------ */
    /* Save return address as the program counter                         */
    /*   In call8 ABI, a0 is the return address (to the caller).          */
    /*   When xeros_ctx_restore loads this into a0 and jumps to it,       */
    /*   execution resumes at the instruction after the call8 ctx_save.   */
    /* ------------------------------------------------------------------ */
    s32i    a0, a2, 92              /* ctx->pc = return address */

    /* ------------------------------------------------------------------ */
    /* Return 0 (save path indicator)                                     */
    /* ------------------------------------------------------------------ */
    movi    a2, 0
    retw                            /* call8 windowed return */

    .size   xeros_ctx_save, . - xeros_ctx_save

/* ========================================================================== */
/*  xeros_ctx_restore                                                          */
/* ========================================================================== */

/**
 * @brief Restore a previously saved execution context (longjmp-like)
 *
 * call8 ABI: After `entry sp, 32`, a2 = ctx (was a10 before rotation).
 *
 * Restores all special registers and general-purpose registers from the
 * context structure, then jumps to the saved program counter. This function
 * never returns to its caller -- execution continues at the point where
 * xeros_ctx_save() was originally called, with a2 = 1 (longjmp indicator).
 *
 * @param a10 ctx  (call8 ABI: first arg in a10)
 *
 * Restoration order:
 *   1. Disable interrupts (rsil to level 3)
 *   2. Restore special registers (SAR, LBEG, LEND, LCOUNT, PS)
 *   3. rsync to ensure PS takes effect
 *   4. Restore a0 (PC), a1 (SP), a3-a15 from context
 *   5. Set a2 = 1 (longjmp return value)
 *   6. Jump to saved PC
 */
    .global xeros_ctx_restore
    .type   xeros_ctx_restore, @function
    .align  4
xeros_ctx_restore:
    entry   sp, 32                  /* call8: allocate frame, rotate window */
    /* After entry: a2 = ctx (was a10 before rotation) */

    /* ------------------------------------------------------------------ */
    /* Step 1: Disable interrupts to prevent corruption during restore    */
    /* ------------------------------------------------------------------ */
    rsil    a3, 3                   /* INTLEVEL = XCHAL_EXCM_LEVEL */

    /* ------------------------------------------------------------------ */
    /* Step 2: Restore special registers from context                     */
    /* ------------------------------------------------------------------ */
    l32i    a3, a2, 64              /* a3 = ctx->sar */
    wsr     a3, SAR

    l32i    a3, a2, 68              /* a3 = ctx->lbeg */
    wsr     a3, LBEG

    l32i    a3, a2, 72              /* a3 = ctx->lend */
    wsr     a3, LEND

    l32i    a3, a2, 76              /* a3 = ctx->lcount */
    wsr     a3, LCOUNT

    l32i    a3, a2, 80              /* a3 = ctx->ps */
    wsr     a3, PS

    /* ------------------------------------------------------------------ */
    /* Step 3: Synchronize after writing processor state                  */
    /* ------------------------------------------------------------------ */
    rsync

    /* ------------------------------------------------------------------ */
    /* Step 4: Restore general-purpose registers from context             */
    /*   Restore a0 (PC), a1 (SP), a3-a15 first.                         */
    /*   Set a2 = 1 LAST (longjmp return value).                         */
    /*   Then jump to saved PC.                                           */
    /* ------------------------------------------------------------------ */
    l32i    a0, a2, 92              /* a0 = ctx->pc (program counter to resume at) */
    l32i    a1, a2, 4               /* a1 = ctx->a1 (saved stack pointer) */
    l32i    a3, a2, 12              /* a3 = ctx->a3 */
    l32i    a4, a2, 16              /* a4 = ctx->a4 */
    l32i    a5, a2, 20              /* a5 = ctx->a5 */
    l32i    a6, a2, 24              /* a6 = ctx->a6 */
    l32i    a7, a2, 28              /* a7 = ctx->a7 */
    l32i    a8, a2, 32              /* a8 = ctx->a8 */
    l32i    a9, a2, 36              /* a9 = ctx->a9 */
    l32i    a10, a2, 40             /* a10 = ctx->a10 */
    l32i    a11, a2, 44             /* a11 = ctx->a11 */
    l32i    a12, a2, 48             /* a12 = ctx->a12 */
    l32i    a13, a2, 52             /* a13 = ctx->a13 */
    l32i    a14, a2, 56             /* a14 = ctx->a14 */
    l32i    a15, a2, 60             /* a15 = ctx->a15 */

    /* Set a2 = 1 (longjmp return value) -- overwrites ctx pointer */
    movi    a2, 1

    /* ------------------------------------------------------------------ */
    /* Step 5: Jump to saved program counter                              */
    /*   a0 was loaded with ctx->pc. Jumping there resumes execution.     */
    /* ------------------------------------------------------------------ */
    jx      a0                      /* jump to saved PC -- never returns here */

    .size   xeros_ctx_restore, . - xeros_ctx_restore

/* ========================================================================== */
/*  xeros_flush_windows                                                        */
/* ========================================================================== */

/**
 * @brief Flush all register windows to the stack
 *
 * In call0 ABI, register windows are not normally used, but this function
 * is provided as a defensive measure. In call8 ABI (our case), this is
 * a no-op since we handle window management explicitly.
 */
    .global xeros_flush_windows
    .type   xeros_flush_windows, @function
    .align  4
xeros_flush_windows:
    ret.n

    .size   xeros_flush_windows, . - xeros_flush_windows

#endif /* !NATIVE_TEST */
