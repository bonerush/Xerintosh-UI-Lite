/* Only compile for Xtensa targets (ESP32), skip on native test builds */
#ifndef NATIVE_TEST

/**
 * @file ctx_switch.S
 * @brief Xeros context switch engine -- Xtensa implementation for ESP32
 *
 * Context save/restore design:
 *
 * xeros_ctx_save: call8 ABI (windowed), NO entry instruction
 *   - Called from C via call8, so a10 = ctx, a8 = return address
 *   - NO entry = stays in kern_sched_tick's register window
 *   - Saves ALL 16 GPRs (all are kern_sched_tick's registers)
 *   - Saves PS as-is (kern_sched_tick's WB, CALLINC=2)
 *   - Returns via jx to cleaned return address (compiler reads a10)
 *
 * xeros_ctx_restore_impl: call0 ABI (no window rotation)
 *   - Called from xeros_ctx_restore wrapper via call0
 *   - Restores all registers from context
 *   - Restores PS flags (CALLINC, WOE, UM) but preserves kern_sched_tick's WB
 *   - Jumps to saved PC via jx a0 (no rotation)
 *   - For resumed tasks: returns to kern_sched_tick, retw rotates correctly
 *   - For new tasks: jumps to trampoline, which calls entry(arg)
 *   - Returns 1 in a10 (longjmp value; no entry = same window as kern_sched_tick)
 *
 * xeros_ctx_restore: call8 ABI wrapper
 *   - Thin wrapper that calls xeros_ctx_restore_impl via call0
 *   - Never returns (ctx_restore_impl jumps away)
 *
 * Context structure layout (kern_ctx_native_t, 96 bytes total):
 *   Offset  0: a0        (kern_sched_tick's return addr to its caller)
 *   Offset  4: a1        (stack pointer)
 *   Offset  8: a2        (general purpose)
 *   Offset 12: a3        (general purpose)
 *   Offset 16: a4        (general purpose)
 *   Offset 20: a5        (general purpose)
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
 * Processor State value for new tasks.
 *   PS.WOE   = 1  (window overflow exceptions ENABLED)
 *   PS.CALLINC = 1  (entry instruction uses call4 convention)
 *   PS.OWB   = 0  (overflow window base = 0)
 *   PS.UM    = 1  (user mode)
 *   PS.EXCM  = 0  (not in exception mode)
 *   PS.INTLEVEL = 3 (interrupts masked during restore)
 *   Bit 18 (WOE) = 1, Bit 17-16 (CALLINC) = 01, Bit 5 (UM) = 1, Bit 3:0 = 3
 *   => 0x00050023
 *
 * CALLINC=1: the trampoline uses call4 to invoke xeros_task_wrapper (a normal
 * C function compiled with call4 convention). The wrapper's `entry` has
 * CALLINC=1 encoded by the compiler, which matches PS.CALLINC=1.
 * WINDOWSTART = 0xFFFF ensures all windows are valid during rotation.
 *
 * Window count: kern_sched(K) -> ctx_restore(K) -> trampoline(K) ->
 *   wrapper(K+1 via entry) -> wrapper(K+2 via callx4) -> task_entry(K+3 via entry).
 *   Overflow handler spills using a5 (= stack_top, set by ctx_init).
 *   WINDOWSTART = 0xFFFF covers all rotations.
 */
    .global xeros_ps_init
    .type   xeros_ps_init, @object
    .align  4
xeros_ps_init:
    .word   0x00050023
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
 * call8 ABI: arguments arrive in a10-a15. After `entry sp, 32`:
 *   a2 = ctx, a3 = stack_base, a4 = stack_size, a5 = entry, a6 = arg
 *
 * New task context layout (CALLINC=1, trampoline + callx4 wrapper):
 *   ctx->a8 = &xeros_task_cleanup_handler  (→ wrapper a0 = retw return addr)
 *   ctx->a9 = aligned stack top            (→ wrapper a1 = SP)
 *   ctx->a10 = entry function              (→ wrapper a2 = pxCode)
 *   ctx->a11 = arg                         (→ wrapper a3 = pvParameters)
 *   ctx->pc = &xeros_task_trampoline       (marker to detect new vs resumed)
 *   ctx->ps = 0x00050023                   (CALLINC=1, WOE=1, UM=1, INTLEVEL=3)
 *
 * Execution flow for new tasks (CALLINC=1, WOE=1):
 *   ctx_restore_impl(call0) sets PS.CALLINC=1, WOE=1, INTLEVEL=3
 *   ctx_restore_impl sets WINDOWSTART = (1<<WB)|(1<<(WB+1))|(1<<(WB+2))
 *   ctx_restore_impl loads all GPR from ctx, then jx a0 (= trampoline)
 *   trampoline: entry sp, 0 (CALLINC=1: rotate +1)
 *   trampoline: callx4 xeros_task_wrapper (CALLINC=1: rotate +1)
 *   wrapper: __attribute__((call4)) ensures callx4 for pxCode (CALLINC=1)
 *   wrapper: pxCode(pvParameters); kern_exit()
 *
 * Register mapping (two rotations of +1 each = total +2):
 *   ctx->a8  → trampoline a4 → wrapper a0 (cleanup handler)
 *   ctx->a9  → trampoline a5 → wrapper a1 (stack_top / SP)
 *   ctx->a10 → trampoline a6 → wrapper a2 (pxCode)
 *   ctx->a11 → trampoline a7 → wrapper a3 (pvParameters)
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

    /* Step 3: Set register targets for CALLINC=1 trampoline + call4 wrapper
     *
     * Two rotations of CALLINC=1 = total rotation +2.
     * First rotation (entry): a4←old a8, a5←old a9, a6←old a10, a7←old a11
     * Second rotation (call4): wrapper a0←trampoline a4, a1←a5, a2←a6, a3←a7 */
    s32i    a3, a2, 4               /* ctx->a1 = stack_top (SP for old window spill during trampoline entry) */
    s32i    a3, a2, 20              /* ctx->a5 = stack_top (S32E save base: entry abort leaves a5 undefined) */
    movi    a7, xeros_task_cleanup_handler
    s32i    a7, a2, 32              /* ctx->a8 = cleanup handler (→ trampoline a4 → wrapper a0) */
    s32i    a3, a2, 36              /* ctx->a9 = stack_top (→ trampoline a5 → wrapper a1 = SP) */
    s32i    a5, a2, 40              /* ctx->a10 = entry function (→ trampoline a6 → wrapper a2 = pxCode) */
    s32i    a6, a2, 44              /* ctx->a11 = arg (→ trampoline a7 → wrapper a3 = pvParameters) */

    /* Step 4: Set trampoline as PC (ctx_restore_impl uses this to detect new tasks) */
    movi    a7, xeros_task_trampoline
    s32i    a7, a2, 92              /* ctx->pc = &xeros_task_trampoline (detection marker) */

    /* Step 5: Set processor state (CALLINC=1, WOE=1, UM=1, INTLEVEL=3) */
    movi    a3, 0x00050023
    s32i    a3, a2, 80              /* ctx->ps = 0x00050023 */

    retw                            /* return (call8 windowed return) */

    .size   xeros_ctx_init, . - xeros_ctx_init

/* ========================================================================== */
/*  xeros_task_trampoline                                                      */
/* ========================================================================== */

/**
 * @brief Trampoline for newly created tasks
 *
 * Reached via jx a0 from ctx_restore_impl. PS.CALLINC=1, WOE=1.
 * WINDOWSTART = 0xFFFF (all 16 windows valid).
 *
 * After ctx_restore_impl loads all GPR and jumps here (WB = K):
 *   a8  = ctx->a8  = cleanup handler
 *   a9  = ctx->a9  = stack_top
 *   a10 = ctx->a10 = entry function (pxCode)
 *   a11 = ctx->a11 = arg (pvParameters)
 *
 * The trampoline's `entry sp, 0` (CALLINC=1) rotates window +1 (WB = K+1).
 * After rotation (CALLINC=1 shifts by 1):
 *   a4 = old a8  = cleanup handler
 *   a5 = old a9  = stack_top
 *   a6 = old a10 = pxCode
 *   a7 = old a11 = pvParameters
 *
 * callx4 xeros_task_wrapper (CALLINC=1) rotates +1 (WB = K+2):
 *   wrapper a0 = trampoline a4 = cleanup handler (retw return addr)
 *   wrapper a1 = trampoline a5 = stack_top (SP)
 *   wrapper a2 = trampoline a6 = pxCode
 *   wrapper a3 = trampoline a7 = pvParameters
 *
 * Wrapper is __attribute__((call4)): compiler generates callx4 for pxCode (CALLINC=1).
 * Wrapper calls pxCode(pvParameters), then kern_exit().
 * kern_exit returns to cleanup handler via retw (a0 = cleanup addr).
 */
    .section .iram1.text, "ax"
    .global xeros_task_trampoline
    .type   xeros_task_trampoline, @function
    .align  4
xeros_task_trampoline:
    /* entry sp, 0 with CALLINC=1:
     *   - Rotates window +1 (WB: K → K+1)
     *   - Allocates 0 bytes on stack (SP unchanged)
     *
     * After rotation: a4 = old a8 (cleanup), a5 = old a9 (stack_top),
     * a6 = old a10 (entry), a7 = old a11 (arg). */
    entry   sp, 0                   /* CALLINC=1: rotate +1, alloc 0 bytes */

    /* DEBUG: write '2' to UART0 TX FIFO */
    movi    a2, 0x3FF40000
    movi    a3, 50                  /* '2' */
    s8i     a3, a2, 0

    /* call4 xeros_task_wrapper: maps a2→a0, a3→a1, a4→a2, a5→a3.
     * Wrapper sees: a0=cleanup, a1=stack_top, a2=pxCode, a3=pvParameters.
     * Wrapper's __attribute__((call4)) ensures callx4 for pxCode (CALLINC=1).
     * Wrapper calls pxCode(pvParameters), then kern_exit(). */
    movi    a4, xeros_task_wrapper  /* a4 = wrapper addr (assembler uses l32r for distant targets) */
    callx4  a4                      /* CALLINC=1: rotate +1, store retaddr in a0 */

    .size   xeros_task_trampoline, . - xeros_task_trampoline

/* ========================================================================== */
/*  xeros_task_wrapper                                                         */
/* ========================================================================== */

/**
 * @brief Task wrapper — assembly implementation for correct calling convention
 *
 * Called by the trampoline via callx4 (CALLINC=1). This ensures the wrapper's
 * entry instruction has CALLINC=1, matching the callx4 from the trampoline.
 *
 * CRITICAL: This function is written in assembly to guarantee callx4 is used
 * for pxCode (not callx8). If pxCode is a call4 function (e.g. idle_entry),
 * callx8 would set CALLINC=2 and cause a return address mismatch:
 *   - callx8 stores retaddr in a8 (CALLINC=2)
 *   - call4's retw.n loads from a0 (CALLINC=1)
 *   - Result: jump to wrong address → crash
 *
 * By using callx4 explicitly, CALLINC=1 is preserved throughout the chain:
 *   trampoline(callx4) → wrapper(callx4) → pxCode
 *
 * After callx4 from trampoline (WB = K+2):
 *   a0 = return address to trampoline (CALLINC=1 encoded)
 *   a1 = stack_top (SP)
 *   a2 = pxCode (entry function)
 *   a3 = pvParameters (task argument)
 *
 * Wrapper's entry a1, 16 (CALLINC=1) rotates K+2 → K+3:
 *   a0 = old a0 = callx4 return addr (overwritten by entry with old SP)
 *   a1 = old a1 = stack_top → SP = SP - 16
 *   a2 = old a2 = pxCode
 *   a3 = old a3 = pvParameters
 *
 * Wrapper calls pxCode(pvParameters) via callx4, then kern_exit() via callx4.
 * Both use CALLINC=1, matching their call4-compiled entry instructions.
 */
    .section .iram1.text, "ax"
    .global xeros_task_wrapper
    .type   xeros_task_wrapper, @function
    .align  4
xeros_task_wrapper:
    entry   a1, 16                  /* CALLINC=1: rotate +1, alloc 16-byte frame */

    /* DEBUG: write '3' to UART0 TX FIFO */
    movi    a4, 0x3FF40000
    movi    a5, 51                  /* '3' */
    s8i     a5, a4, 0

    /* After entry: a2 = pxCode, a3 = pvParameters.
     * Set up a2 = pvParameters (arg), call pxCode via callx4. */
    mov     a6, a2                  /* a6 = pxCode (save before clobbering a2) */
    mov     a2, a3                  /* a2 = pvParameters (first arg to pxCode) */
    callx4  a6                      /* CALLINC=1: call pxCode(pvParameters) */

    /* pxCode returned. Call kern_exit() — never returns. */
    movi    a2, kern_exit
    callx4  a2                      /* CALLINC=1: call kern_exit() */

    /* Should never reach here */
.L_wrapper_halt:
    j       .L_wrapper_halt

    .size   xeros_task_wrapper, . - xeros_task_wrapper

/* ========================================================================== */
/*  xeros_task_cleanup_handler                                                 */
/* ========================================================================== */

/**
 * @brief Cleanup handler for tasks that return from their entry function
 *
 * When the entry function returns via retw, execution lands here
 * (the instruction after callx8 in the trampoline).
 *
 * Window count at this point: trampoline(K+4). We use call0 (NOT callx8)
 * to call xeros_ctx_restore_impl, which does NOT rotate the window.
 * This avoids consuming additional window levels.
 */
    .section .iram1.text, "ax"
    .global xeros_task_cleanup_handler
    .type   xeros_task_cleanup_handler, @function
    /* No .align 4 here -- must be contiguous with trampoline for callx8 fall-through */
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
 * NO entry instruction -- stays in kern_sched_tick's register window.
 * After call8 from kern_sched_tick: a10 = ctx, a8 = return address.
 *
 * CRITICAL: Without entry, we share kern_sched_tick's register window.
 * a10 IS kern_sched_tick's a10 (same physical register). The compiler
 * reads the return value from a10 after call8, so we must set a10 (not a2)
 * to the return value before returning.
 *
 * call8 stores return address at R[(WB+CALLINC)*4] = a8 (not a0!).
 * kern_sched_tick's a0 (its return addr to its caller) is preserved.
 *
 * Returns 0 on the save path. When restored via xeros_ctx_restore(),
 * execution resumes at kern_sched_tick and returns 1.
 *
 * @param a10 ctx  (call8 ABI: first arg in a10)
 * @return 0 (save path) or 1 (restore path)
 */
    .global xeros_ctx_save
    .type   xeros_ctx_save, @function
    .align  4
xeros_ctx_save:
    /* NO entry instruction. a10 = ctx, a8 = return address (from call8).
     * PS.CALLINC = 2. WB = kern_sched_tick's WB (unchanged).
     *
     * ALL registers are kern_sched_tick's. Save FIRST, then use scratch. */

    /* Step 1: Save all 16 GPRs (a10 = ctx base pointer) */
    s32i    a0, a10, 0              /* ctx->a0  (kern_sched_tick's return addr) */
    s32i    a1, a10, 4              /* ctx->a1  (stack pointer) */
    s32i    a2, a10, 8              /* ctx->a2 */
    s32i    a3, a10, 12             /* ctx->a3 */
    s32i    a4, a10, 16             /* ctx->a4 */
    s32i    a5, a10, 20             /* ctx->a5 */
    s32i    a6, a10, 24             /* ctx->a6 */
    s32i    a7, a10, 28             /* ctx->a7 */
    s32i    a8, a10, 32             /* ctx->a8  (return addr with CALLINC=2) */
    s32i    a9, a10, 36             /* ctx->a9 */
    s32i    a10, a10, 40            /* ctx->a10 */
    s32i    a11, a10, 44            /* ctx->a11 */
    s32i    a12, a10, 48            /* ctx->a12 */
    s32i    a13, a10, 52            /* ctx->a13 */
    s32i    a14, a10, 56            /* ctx->a14 */
    s32i    a15, a10, 60            /* ctx->a15 */

    /* Step 2: Clean return address and save as ctx->pc
     * call8 stores return addr with CALLINC=2 in bits [31:30] (0x80xxxxxx).
     * XOR 0xC0000000 flips to 0x40xxxxxx (valid flash address).
     * retw gets CALLINC from the window (set by call8), not from ctx->pc. */
    l32i    a8, a10, 32             /* a8 = raw return address */
    movi    a9, -1
    slli    a9, a9, 30              /* a9 = 0xC0000000 */
    xor     a8, a8, a9              /* a8 = clean 0x40xxxxxx address */
    s32i    a8, a10, 92             /* ctx->pc = clean address */

    /* Step 3: Save special registers */
    rsr     a3, SAR;    s32i a3, a10, 64
    rsr     a3, LBEG;   s32i a3, a10, 68
    rsr     a3, LEND;   s32i a3, a10, 72
    rsr     a3, LCOUNT; s32i a3, a10, 76
    rsr     a3, PS;     s32i a3, a10, 80
    rsr     a3, EXCCAUSE; s32i a3, a10, 84
    rsr     a3, EXCVADDR; s32i a3, a10, 88
    rsr     a3, WINDOWBASE; s32i a3, a10, 96  /* ctx->windowbase */

    /* Step 4: Restore clobbered registers and return
     * a3 and a9 were used as scratch. a10 = return value (0 = save path).
     * a10 IS kern_sched_tick's a10 (no entry = same window). */
    l32i    a3, a10, 12             /* restore a3 */
    l32i    a9, a10, 36             /* restore a9 */
    l32i    a8, a10, 92             /* a8 = clean return address */
    movi    a10, 0                  /* return value = 0 (kern_sched_tick reads a10) */
    jx      a8                      /* return to kern_sched_tick */

    .size   xeros_ctx_save, . - xeros_ctx_save

/* ========================================================================== */
/*  xeros_ctx_restore                                                          */
/* ========================================================================== */

/**
 * @brief Restore a previously saved execution context (call8 wrapper)
 *
 * call8 ABI: called from C, so a10 = ctx.
 * NO entry instruction -- stays in caller's (kern_sched_tick's) register window.
 * Thin wrapper that calls xeros_ctx_restore_impl via call0.
 * Never returns (ctx_restore_impl jumps away).
 *
 * @param a10 ctx  (call8 ABI: first arg in a10)
 */
    .global xeros_ctx_restore
    .type   xeros_ctx_restore, @function
    .align  4
xeros_ctx_restore:
    /* Stay in caller's window (no entry). a10 = ctx.
     * Call ctx_restore_impl via call0 (a2 = ctx). */
    mov     a2, a10                 /* a2 = ctx (call0 first arg) */
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
 *   - Never returns (jumps to saved PC)
 *
 * Restores all special registers and general-purpose registers from the
 * context structure, then jumps to the saved program counter.
 *
 * For RESUMED tasks: ctx->ps has CALLINC from save. ctx->pc = return address
 *   to kern_sched_tick. After jx a0, execution resumes in kern_sched_tick.
 *
 * For NEW tasks: ctx->ps has CALLINC=1, WOE=1, UM=1, INTLEVEL=3.
 *   WINDOWSTART = 0xFFFF (all 16 windows valid).
 *   After jx a0, execution begins at the trampoline (= ctx->pc).
 *   The trampoline's `entry sp, 0` (CALLINC=1) rotates +1 (WB: K → K+1).
 *   Trampoline calls xeros_task_wrapper via callx4 (CALLINC=1).
 *   Wrapper's `entry a1, 16` rotates +1 (WB: K+1 → K+2).
 *   Wrapper calls pxCode(pvParameters) via callx4 (CALLINC=1).
 *   Wrapper calls kern_exit() via callx4 (CALLINC=1).
 *   kern_exit returns to cleanup handler via retw.
 *   Cleanup: call0 xeros_ctx_restore_impl with g_sched_ctx.
 *
 * @param a2 ctx  (call0 ABI: first arg in a2)
 */
    .global xeros_ctx_restore_impl
    .type   xeros_ctx_restore_impl, @function
    .align  4
xeros_ctx_restore_impl:
    /* No entry instruction -- we stay in the caller's window. */

    /* DEBUG: write '0' to UART0 TX FIFO (impl entry) */
    movi    a3, 0x3FF40000
    movi    a4, 48                  /* '0' */
    s8i     a4, a3, 0

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

    /* DEBUG: write '1' after SAR */
    movi    a3, 0x3FF40000
    movi    a4, 49                  /* '1' */
    s8i     a4, a3, 0

    l32i    a3, a2, 68              /* a3 = ctx->lbeg */
    wsr     a3, LBEG

    /* DEBUG: write '2' after LBEG */
    movi    a3, 0x3FF40000
    movi    a4, 50                  /* '2' */
    s8i     a4, a3, 0

    l32i    a3, a2, 72              /* a3 = ctx->lend */
    wsr     a3, LEND

    /* DEBUG: write '3' after LEND */
    movi    a3, 0x3FF40000
    movi    a4, 51                  /* '3' */
    s8i     a4, a3, 0

    l32i    a3, a2, 76              /* a3 = ctx->lcount */
    wsr     a3, LCOUNT

    /* DEBUG: write '4' after LCOUNT */
    movi    a3, 0x3FF40000
    movi    a4, 52                  /* '4' */
    s8i     a4, a3, 0

    /* ------------------------------------------------------------------ */
    /* Step 2b: Restore WINDOWBASE from context                           */
    /*   Must be done BEFORE PS restore (PS.OWB is a shadow used by       */
    /*   exception handlers; the real WINDOWBASE controls which physical   */
    /*   registers are visible as a0-a15).                                 */
    /* ------------------------------------------------------------------ */
    l32i    a3, a2, 96              /* a3 = ctx->windowbase */
    wsr     a3, WINDOWBASE

    /* Re-enable all 16 window positions so window rotations don't trap */
    movi    a3, 0xFFFF
    wsr     a3, WINDOWSTART
    rsync

    /* DEBUG: write 'C' (reached WINDOWSTART section) */
    movi    a3, 0x3FF40000
    movi    a4, 67                  /* 'C' */
    s8i     a4, a3, 0

.L_ws_done:

    /* Restore PS: take CALLINC/WOE/UM/etc from ctx->ps, but KEEP the current
     * Window Base (WB, bits [11:8]).
     *
     * For NEW tasks: set CALLINC=1 (trampoline uses call4 to wrapper), WOE=1
     *   (window overflow exceptions enabled for rotation), UM=1 (user mode),
     *   INTLEVEL=3 (mask interrupts during restore).
     *   ctx_restore_impl jumps to trampoline via jx a5 (= ctx->pc).
     *   Trampoline's `entry sp, 0` (CALLINC=1) rotates window +1.
     *
     * For RESUMED tasks: restore PS fully from ctx->ps (including the correct
     *   WB from when the task was saved). */
    l32i    a4, a2, 80              /* a4 = ctx->ps */
    l32i    a5, a2, 92              /* a5 = ctx->pc */
    movi    a6, xeros_task_trampoline
    bne     a5, a6, .L_ps_resumed   /* if resumed task, use full ctx->ps */

    /* New task: CALLINC=1, WOE=1, UM=1, INTLEVEL=3.
     * Keep current WB (bits [11:8]) — trampoline will rotate from here. */
    rsr     a3, PS                  /* a3 = current PS (has correct WB) */
    movi    a4, 0xF00               /* WB mask: bits [11:8] */
    and     a3, a3, a4              /* a3 = current WB only */
    movi    a4, 0x50023             /* CALLINC=1 (bits 17:16=01), WOE=1 (bit 18),
                                       UM=1 (bit 5), INTLEVEL=3 (bits 3:0) */
    or      a3, a3, a4              /* WB | CALLINC=1 | WOE=1 | UM=1 | INTLEVEL=3 */
    j       .L_ps_write

.L_ps_resumed:
    /* Resumed task: use full ctx->ps (includes correct WB, CALLINC, etc.) */
    mov     a3, a4                  /* a3 = ctx->ps (full restore) */

.L_ps_write:
    wsr     a3, PS
    rsync                           /* PS takes effect */

    /* DEBUG: write 'P' to UART0 TX FIFO (after PS rsync) */
    movi    a3, 0x3FF40000
    movi    a4, 80                  /* 'P' */
    s8i     a4, a3, 0

    /* Padding: ensure instruction alignment after PS restore */
    nop
    nop

    /* ------------------------------------------------------------------ */
    /* Step 3: Restore general-purpose registers from context             */
    /*   a2 is the base pointer. Must load a0 before a2 itself.          */
    /*   For new tasks: CALLINC=1, jump to trampoline via jx a0.         */
    /*   For resumed tasks: a10 = 1 (longjmp return value), jx a0.      */
    /*   We must decide BEFORE a2 is overwritten (it's the last load).   */
    /* ------------------------------------------------------------------ */

    l32i    a3, a2, 92              /* a3 = ctx->pc (temp: check new/resumed) */
    movi    a4, xeros_task_trampoline
    beq     a3, a4, .L_new_task     /* if pc == trampoline, it's a new task */

    /* Resumed task: a10 = 1 (longjmp return value) */
    l32i    a1, a2, 4               /* a1 = ctx->a1 (saved stack pointer) */
    l32i    a3, a2, 12              /* a3 = ctx->a3 */
    l32i    a4, a2, 16              /* a4 = ctx->a4 */
    l32i    a5, a2, 20              /* a5 = ctx->a5 */
    l32i    a6, a2, 24              /* a6 = ctx->a6 */
    l32i    a7, a2, 28              /* a7 = ctx->a7 */
    l32i    a8, a2, 32              /* a8 = ctx->a8 */
    l32i    a9, a2, 36              /* a9 = ctx->a9 */
    movi    a10, 1                  /* a10 = 1 (longjmp return value) */
    l32i    a11, a2, 44             /* a11 = ctx->a11 */
    l32i    a12, a2, 48             /* a12 = ctx->a12 */
    l32i    a13, a2, 52             /* a13 = ctx->a13 */
    l32i    a14, a2, 56             /* a14 = ctx->a14 */
    l32i    a15, a2, 60             /* a15 = ctx->a15 */
    l32i    a0, a2, 92              /* a0 = ctx->pc (clean flash address) */
    l32i    a2, a2, 8               /* a2 = ctx->a2 (overwrites base -- LAST) */
    jx      a0                      /* jump to saved PC (resumed task) */

.L_new_task:
    /* New task: CALLINC=1, jump to trampoline.
     *
     * Load all GPR from context. a0 = trampoline address (jx target).
     * Two rotations (entry + call4) shift registers by +2:
     *   ctx->a8  → trampoline a4 → wrapper a0 (cleanup handler)
     *   ctx->a9  → trampoline a5 → wrapper a1 (stack_top / SP)
     *   ctx->a10 → trampoline a6 → wrapper a2 (pxCode)
     *   ctx->a11 → trampoline a7 → wrapper a3 (pvParameters) */
    l32i    a0, a2, 92              /* a0 = ctx->pc = trampoline (jx target) */
    l32i    a1, a2, 4               /* a1 = ctx->a1 (SP: old window needs valid SP for entry spill) */
    l32i    a3, a2, 12              /* a3 = ctx->a3 */
    l32i    a4, a2, 16              /* a4 = ctx->a4 */
    l32i    a5, a2, 20              /* a5 = ctx->a5 */
    l32i    a6, a2, 24              /* a6 = ctx->a6 */
    l32i    a7, a2, 28              /* a7 = ctx->a7 */
    l32i    a8, a2, 32              /* a8 = ctx->a8 (cleanup handler) */
    l32i    a9, a2, 36              /* a9 = ctx->a9 (stack_top) */
    l32i    a10, a2, 40             /* a10 = ctx->a10 (entry/pxCode) */
    l32i    a11, a2, 44             /* a11 = ctx->a11 (arg/pvParameters) */
    l32i    a12, a2, 48             /* a12 = ctx->a12 */
    l32i    a13, a2, 52             /* a13 = ctx->a13 */
    l32i    a14, a2, 56             /* a14 = ctx->a14 */
    l32i    a15, a2, 60             /* a15 = ctx->a15 */

    /* DEBUG: write '1' to UART0 TX FIFO */
    movi    a3, 0x3FF40000
    movi    a4, 49                  /* '1' */
    s8i     a4, a3, 0

    jx      a0                      /* jump to trampoline (a2 not needed, trampoline uses a4-a7 after rotation) */

    .size   xeros_ctx_restore_impl, . - xeros_ctx_restore_impl

#endif /* !NATIVE_TEST */
