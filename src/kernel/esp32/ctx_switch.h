/**
 * @file ctx_switch.h
 * @brief Xeros 原生上下文切换引擎 — 基于 Xtensa call8 ABI 的 ESP32 实现
 *
 * 本文件定义了 Xeros 内核在 ESP32 (Xtensa LX6) 上进行上下文切换所需的
 * 原生上下文结构体和相关函数声明。所有寄存器保存/恢复操作均在汇编文件
 * ctx_switch.S 中实现。
 *
 * @note ESP32 Xtensa GCC 默认使用 call8（窗口化）ABI。
 *       函数参数通过 a10-a15 传递，`entry sp, 32` 旋转寄存器窗口后
 *       参数出现在 a2-a7。汇编函数必须与编译器的 ABI 选择一致。
 */

#ifndef XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H
#define XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 原生上下文结构体 — 保存 Xtensa 线程的完整寄存器快照
 *
 * 该结构体对应一次上下文切换所需保存的全部处理器状态。
 * 共保存 24 个 32 位寄存器，总计 96 字节。
 * 布局必须与 ctx_switch.S 中的汇编偏移量严格一致。
 *
 * 对于新任务：
 *   a0 指向清理处理器（entry 返回后调用 kern_exit）
 *   a8-a11 存放清理 handler、栈顶、entry、arg，pc 指向蹦床函数。
 *   蹦床通过 call8 调用 xeros_task_wrapper（默认 call8 ABI），
 *   wrapper 再通过 call8 调用 entry(arg)，保持整个链路的 CALLINC=2。
 *
 * @note ctx_save 使用 call8 ABI 保存寄存器，然后清除 CALLINC 后通过
 *       call0 子函数返回（避免 retw 旋转窗口）。
 *       ctx_restore 通过 call8 包装器调用 call0 实现函数，
 *       恢复 PS（包含 CALLINC=2）后通过 jx 跳转（不触发窗口旋转）。
 *       这与 FreeRTOS ESP32 port 的方案一致：上下文保存/恢复使用 call0，
 *       任务级代码使用 call8（窗口化调用约定）。
 */
typedef struct {
    /* ---- 通用寄存器 (a0-a15) ---- */

    uint32_t a0;   /**< 返回地址寄存器（硬件自动保存的旧 PC） */
    uint32_t a1;   /**< 栈指针寄存器 (SP) */
    uint32_t a2;   /**< 通用寄存器 / 函数第一个参数 */
    uint32_t a3;   /**< 通用寄存器 / 函数第二个参数 */
    uint32_t a4;   /**< 通用寄存器 / 函数第三个参数 */
    uint32_t a5;   /**< 通用寄存器 / 函数第四个参数 */
    uint32_t a6;   /**< 通用寄存器 */
    uint32_t a7;   /**< 通用寄存器 */
    uint32_t a8;   /**< 通用寄存器 */
    uint32_t a9;   /**< 通用寄存器 */
    uint32_t a10;  /**< 通用寄存器 */
    uint32_t a11;  /**< 通用寄存器 */
    uint32_t a12;  /**< 通用寄存器 */
    uint32_t a13;  /**< 通用寄存器 */
    uint32_t a14;  /**< 通用寄存器 */
    uint32_t a15;  /**< 通用寄存器 */

    /* ---- 特殊功能寄存器 ---- */

    uint32_t sar;      /**< 移位量寄存器 (Shift Amount Register) */
    uint32_t lbeg;     /**< 零开销循环起始地址寄存器 (Loop Begin) */
    uint32_t lend;     /**< 零开销循环结束地址寄存器 (Loop End) */
    uint32_t lcount;   /**< 零开销循环计数寄存器 (Loop Count) */
    uint32_t ps;       /**< 处理器状态寄存器 (Processor State) */

    /* ---- 异常信息 ---- */

    uint32_t exccause; /**< 异常原因码 (Exception Cause) */
    uint32_t excvaddr; /**< 异常触发地址 (Exception Virtual Address) */

    /* ---- 程序计数器 ---- */

    uint32_t pc;       /**< 程序计数器 / 上下文恢复入口地址 (Program Counter) */

    /* ---- 窗口寄存器 ---- */

    uint32_t windowbase;  /**< 窗口基址寄存器 (Window Base, SR 72) */
    uint32_t windowstart; /**< 窗口起始寄存器 (Window Start, SR 73) */
} kern_ctx_native_t;

/**
 * @brief 原生上下文结构体的大小（字节数）
 *
 * 等于 26 个 uint32_t 字段 = 104 字节。汇编代码中使用此常量
 * 计算各寄存器在结构体内的偏移量。
 */
#define KERN_CTX_NATIVE_SIZE   (26 * sizeof(uint32_t))

/* ========================================================================== */
/*  函数声明                                                                   */
/* ========================================================================== */

/**
 * @brief 初始化一个新上下文，使其可以被 xeros_ctx_restore() 启动
 *
 * 该函数设置上下文的栈指针、入口地址和参数，使上下文在首次被
 * xeros_ctx_restore() 恢复时从指定的入口函数开始执行。
 *
 * @param[out] ctx         指向要初始化的上下文结构体
 * @param[in]  stack_base  栈内存的起始地址（低地址端）
 * @param[in]  stack_size  栈的大小（字节数）
 * @param[in]  entry       上下文启动后要执行的入口函数
 * @param[in]  arg         传递给入口函数的第一个参数
 *
 * @note 栈指针将被设置为 stack_base + stack_size（满递减栈），
 *       并按 16 字节对齐（Xtensa ABI 要求）。
 */
void xeros_ctx_init(kern_ctx_native_t *ctx,
                    void *stack_base,
                    size_t stack_size,
                    void (*entry)(void *),
                    void *arg);

/**
 * @brief 保存当前上下文（setjmp 语义，纯汇编实现）
 *
 * 将当前所有通用寄存器和特殊寄存器保存到 @p ctx 中。
 * 该函数使用 setjmp 语义：
 *   - 首次调用（保存时）返回 0
 *   - 后续从 xeros_ctx_restore() 恢复时返回 1
 *
 * @note 纯汇编实现（ctx_switch.S）。内部使用 call0 ABI 进行实际的
 *       寄存器保存操作，通过 call8 包装器从 C 代码调用。
 *       call0 不旋转寄存器窗口，避免了恢复不同任务上下文时
 *       旧窗口栈指针无效导致的 WindowOverflow 崩溃。
 *
 * @param[out] ctx 指向用于保存寄存器状态的上下文结构体
 * @return 0 表示刚完成保存；1 表示从恢复路径返回
 */
int xeros_ctx_save(kern_ctx_native_t *ctx);

/**
 * @brief 恢复一个已保存的上下文（longjmp 语义，纯汇编实现）
 *
 * 从 @p ctx 中恢复所有寄存器状态，并跳转到之前保存的 PC 位置继续执行。
 * 该函数不会返回到调用者，而是直接切换到目标上下文。
 *
 * @note 纯汇编实现（ctx_switch.S）。内部使用 call0 ABI 进行实际的
 *       寄存器恢复操作，通过 call8 包装器从 C 代码调用。
 *       对于恢复的任务，PS.CALLINC=2 确保后续 retw 正确旋转回调用者窗口。
 *
 * @param[in] ctx 指向要恢复的上下文结构体
 *
 * @note 此函数永不返回。它使之前调用 xeros_ctx_save() 的位置返回 1。
 */
void xeros_ctx_restore(kern_ctx_native_t *ctx);

/**
 * @brief 将 Xtensa 寄存器窗口刷写到内存中
 *
 * 执行 `call8` / `rotw` 等操作将当前窗口化的寄存器帧刷写到栈上，
 * 确保在上下文切换前所有寄存器窗口内容都已持久化。
 * 在 call0 ABI 下通常不需要此函数，但作为安全措施仍然提供。
 *
 * @note 这是一个独立的汇编函数，不接受参数，也不返回有意义的值。
 */
void xeros_flush_windows(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* XERINTOSH_KERNEL_ESP32_CTX_SWITCH_H */
