# Demystifying Xtensa ISA

> **原文作者:** sachin0x18  
> **原文链接:** https://sachin0x18.github.io/posts/demystifying-xtensa-isa  
> **转载用途:** 作为 Xeros ESP32 原生上下文切换调试的参考资料。版权归属原作者。

---

"Instruction Set Architecture (ISA) is considered to be esoteric even among software developers." The compiler handles conversion from higher-level languages, but "hand written assembly code dominates when programming performance critical routines."

## Overview of Xtensa

"Xtensa is a post-RISC ISA" with 24-bit instructions (some 16-bit) for "code compactness."

**Registers:**
- PC = Program Counter
- AR = General purpose registers
- SAR = Shifts and Shift Amount Register

"AR is general purpose registers, there are 64 32-bit registers however only 16 of them are visible/accessible at a time."

## Windowed Register

"The Xtensa core can only access 16 GPR, namely `a0` - `a15`." With windowed register option enabled, "the register frame (a0 - a15) acts as a window, through which only 16 registers are visible, that slides on this large register file having 64 registers."

"Which 16 registers are visible is controlled by the `WindowBase` register." The window starts at **`(WindowBase x 4)`**th position and "shifting/rotation of this window occurs in units of 4."

## Calling Convention

Xtensa supports two ABIs:
1. Windowed register ABI
2. Call0 ABI

### Windowed Register Calling Convention

"Return address is stored in `a0` and the stack pointer is store in `a1`"

| Register | Use |
|----------|-----|
| a0 | Return address |
| a1 | Stack pointer |
| a2 - a7 | Incoming arguments |

"Arguments to the functions are passed in both, registers and memory (stack). The first six arguments are passed in the registers and remaining go on the stack."

Return values are "returned in registers beginning from `a2` till `a5`."

Subroutine calls use `CALLN` and `CALLXN` instructions. "N is the windowed register option that specifies the amount by which the register window needs to be rotated for the callee. N can take values from 0, 4, 8 and 12."

"WindowBase register is incremented by (N/4)" on call. For `callN`/`callxN`:

```
aN of caller will be a0 of callee
a(N+1) of caller will be a1 of callee
and so on…
```

Example:
```asm
func:
    ...
    mov a10, x    // a10 is bar's a2
    mov a11, y    // a11 is bar's a3
    call8 bar
    mov foo, a10  // a10 is bar's a2 (return value)
    ...
```

"The callee function internally will still use `a2` to access its first argument but as you can see, `a2` of the caller is at a different physical location than `a2` of callee."

"When the program tries to write to a register that already has the data of one of the parent routine, a window overflow exception is generated."

## Stack Layout

"`a1` register... always points to the **bottom** of the stack!"

`ENTRY` instruction serves as function prologue:
1. "Allocates the stack frame for the function and sets the stack pointer."
2. "Moves/rotates the register window by n"

Stack layout details:
- "stack grows downwards"
- Outgoing arguments (beyond first 6) at positive offsets from sp
- Local variables above outgoing arguments
- `Base Save Area` (16 bytes) below sp: "reserved for saving the `a0` - `a3` of the caller"
- `Extra Save Area`: "If more registers of the caller are required to be saved"

Example with `call8` starting at WindowBase = 4:

```
Functions:  A -> B -> C -> D -> E -> F -> G -> H -> I
WindowBase: 4    6    8   10   12   14    0    2    4
```

"On the 9th function call the window wraps around." When H modifies `a8`,`a9`.. containing A's data:
- "`a0` - `a3` are stored in the `Base Save Area` of **B**'s stack frame"
- "`a4` - `a7` are stored in the `Extra Save Area` of **A**'s stack frame"

"Whenever **B** returns, window underflow exception will be generated" to restore values.
