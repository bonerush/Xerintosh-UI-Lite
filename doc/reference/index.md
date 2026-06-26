# 参考文档索引

> **Parent:** [知识地图](../index.md)

本目录保存与 Xeros ESP32 原生上下文切换实现相关的参考资料，便于对照 FreeRTOS 官方实现和 Xtensa ABI 文档进行调试与修正。

## 已下载的本地参考

| 文件 | 说明 | 来源 |
|------|------|------|
| [`freertos-portasm.S`](freertos-portasm.S) | FreeRTOS ESP32 Xtensa 调度/上下文切换入口 (`XT_RTOS_INT_ENTER`, `XT_RTOS_INT_EXIT`, `XT_RTOS_PEND_CSW`) | Espressif ESP-IDF v4.4 (MIT License) |
| [`freertos-xtensa_context.S`](freertos-xtensa_context.S) | FreeRTOS Xtensa 上下文保存/恢复汇编 (`_xt_context_save`, `_xt_context_restore`) | Espressif ESP-IDF v4.4 (MIT License) |
| [`freertos-xtensa_context.h`](freertos-xtensa_context.h) | Xtensa 上下文帧结构体定义与宏 | Espressif ESP-IDF v4.4 (MIT License) |
| [`demystifying-xtensa-isa.md`](demystifying-xtensa-isa.md) | 中文翻译后的 Xtensa 窗口寄存器、调用约定、栈布局入门博客 | sachin0x18 |

## 外部参考链接

- [Xtensa Instruction Set Architecture (ISA) Reference Manual (PDF)](https://0x04.net/~mwk/doc/xtensa.pdf) — 官方指令集参考。
- [Xtensa ISA Summary (PDF)](https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf) — Cadence 提供的 ABI 摘要。
- [Demystifying Xtensa ISA (原文)](https://sachin0x18.github.io/posts/demystifying-xtensa-isa) — 关于窗口寄存器与调用约定的博客原文。

## 重点关注内容

调试 `src/kernel/esp32/ctx_switch.S` 时，应重点对照：

1. **窗口旋转映射**：`call8` 调用时 `WindowBase += 2`，调用者的 `a10/a11` 分别映射为被调用者的 `a2/a3`；返回值由被调用者的 `a2` 映射回调用者的 `a10`。
2. **`entry` 指令语义**：分配栈帧并旋转窗口，进入函数后参数寄存器名会变化。
3. **`retw` 语义**：根据 `a0[31:30]` 得到 `CALLINC`，逆向旋转窗口并返回。
4. **栈帧 Base Save Area / Extra Save Area**：理解窗口溢出/下溢时硬件自动保存的位置。
5. **FreeRTOS `portasm.S` 中的 `XT_RTOS_PEND_CSW` 与 `XT_RTOS_INT_EXIT`**：观察其如何安全地切换任务上下文。
