/**
 * @file   kern_mpu.h
 * @brief  Xeros MPU 内存保护头文件
 * @details 定义 MPU 区域类型、每任务配置结构和运行时 API。
 *          当 CONFIG_MPU_ENABLED 未定义时，所有操作退化为空操作，
 *          实现零开销向后兼容。
 *
 *          ESP32 有 8 个 MPU 区域（每个核独立），其中：
 *          - 区域 0-1: 内核代码/数据（特权级只读）
 *          - 区域 2-3: 栈守卫（1 页检测越界）
 *          - 区域 4-5: 任务数据段（用户级读写）
 *          - 区域 6-7: 预留（外设映射等）
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_MPU_H
#define KERN_MPU_H

#include "kern_types.h"

/* 前向声明（避免循环依赖 kern_task.h ↔ kern_mpu.h） */
struct kern_task;

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define KERN_MPU_MAX_REGIONS    8    /* ESP32: 每核最多 8 个 MPU 区域 */
#define KERN_MPU_MIN_ALIGN      32   /* MPU 区域对齐要求（字节） */
#define KERN_MPU_GUARD_PAGES    1    /* 栈守卫页数 */

/* ═══ MPU 区域属性 ═══ */

typedef enum {
    KERN_MPU_ACCESS_NONE  = 0,   /* 禁止访问 */
    KERN_MPU_ACCESS_RO    = 1,   /* 只读 */
    KERN_MPU_ACCESS_RW    = 2,   /* 读写 */
    KERN_MPU_ACCESS_RX    = 3,   /* 读执行 */
    KERN_MPU_ACCESS_RWX   = 4,   /* 读写执行 */
} kern_mpu_access_t;

typedef enum {
    KERN_MPU_TYPE_UNUSED      = 0,  /* 未使用 */
    KERN_MPU_TYPE_KERNEL      = 1,  /* 内核代码/数据（特权级） */
    KERN_MPU_TYPE_STACK_GUARD = 2,  /* 栈守卫页（溢出检测） */
    KERN_MPU_TYPE_TASK_DATA   = 3,  /* 任务数据段（隔离） */
    KERN_MPU_TYPE_PERIPHERAL  = 4,  /* 外设映射 */
} kern_mpu_region_type_t;

/* ═══ MPU 区域描述符 ═══ */

typedef struct kern_mpu_region {
    void                 *base;       /* 区域起始地址（须按 KERN_MPU_MIN_ALIGN 对齐） */
    size_t                size;       /* 区域大小（字节） */
    kern_mpu_access_t     access;     /* 访问权限 */
    kern_mpu_region_type_t type;     /* 区域类型 */
    bool                  enabled;    /* 是否启用 */
} kern_mpu_region_t;

/* ═══ 每任务 MPU 配置 ═══ */

/**
 * @brief 每任务 MPU 配置
 * @note  挂载在 TCB->mpu_config 上，在上下文切换时应用。
 *         包含最多 KERN_MPU_MAX_REGIONS 个区域描述符。
 *         内核区域（type=KERNEL）在所有任务间共享，其他区域按任务隔离。
 */
typedef struct kern_mpu_config {
    uint8_t            region_count;                /* 已配置的区域数 */
    kern_mpu_region_t  regions[KERN_MPU_MAX_REGIONS];  /* 区域配置数组 */
} kern_mpu_config_t;

/* ═══ MPU 运行时 API ═══ */

#ifdef CONFIG_MPU_ENABLED

/**
 * @brief  初始化 MPU 子系统
 * @note   为内核代码和数据设置基础保护区。
 *         ESP32: 配置每个核的 8 个 MPU 区域为默认值（全允许）。
 */
extern void kern_mpu_init(void);

/**
 * @brief  应用指定任务的 MPU 配置
 * @param  task 目标任务（NULL 表示清除 MPU 配置）
 * @note   在上下文切换时调用。ESP32: 写入 MPU 寄存器。
 *         Native: 验证区域对齐（无实际操作）。
 */
extern void kern_mpu_apply(struct kern_task *task);

/**
 * @brief  为任务配置栈守卫区域
 * @param  task       目标任务
 * @param  stack_base 栈基地址
 * @param  stack_size 栈大小
 * @note   在栈底部设置 KERN_MPU_GUARD_PAGES 页的不可访问区域。
 *         栈溢出时触发 MPU 异常（ESP32: IllegalAccess）。
 */
extern void kern_mpu_setup_stack_guard(struct kern_task *task,
                                       void *stack_base, size_t stack_size);

/**
 * @brief  向任务 MPU 配置添加数据区域
 * @param  task   目标任务
 * @param  base   区域起始地址
 * @param  size   区域大小
 * @param  access 访问权限
 * @return KERN_OK 成功，< 0 为错误码
 */
extern kern_err_t kern_mpu_add_region(struct kern_task *task,
                               void *base, size_t size, kern_mpu_access_t access);

#else /* !CONFIG_MPU_ENABLED — 零开销退化 */

#define kern_mpu_init()                    do {} while (0)
#define kern_mpu_apply(t)                  do { (void)(t); } while (0)
#define kern_mpu_setup_stack_guard(t,b,s)  do { (void)(t); (void)(b); (void)(s); } while (0)

static inline kern_err_t kern_mpu_add_region(struct kern_task *task,
                                      void *base, size_t size,
                                      kern_mpu_access_t access)
{
    (void)task; (void)base; (void)size; (void)access;
    return KERN_OK;
}

#endif /* CONFIG_MPU_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* KERN_MPU_H */
