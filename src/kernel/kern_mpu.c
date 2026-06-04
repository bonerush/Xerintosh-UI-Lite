/**
 * @file   kern_mpu.c
 * @brief  Xeros MPU 内存保护实现
 * @details 实现 MPU 子系统初始化、区域配置和上下文切换时的 MPU 应用。
 *          当 CONFIG_MPU_ENABLED 未定义时，所有实现以空操作的 inline/macro
 *          形式在头文件中提供（零开销）。
 *
 *          ESP32 硬件详细：
 *          - DPORT_PRO_*_REG: PRO_CPU (Core 0)
 *          - DPORT_APP_*_REG: APP_CPU (Core 1)
 *          - 8 个区域，通过 DPORT_PMS_* 寄存器配置
 *          - 区域大小必须为 2^n 且 >= 32 字节
 *
 * @copyright Copyright (c) 2026
 */

#include "kern_mpu.h"
#include "kern_task.h"
#include "kern_init.h"

#ifdef CONFIG_MPU_ENABLED

#include <string.h>

/*
 * ═══ ESP32 MPU 寄存器定义 ═══
 *
 * ESP32 有两组 MPU（PMS: Processor Memory Subsystem），
 * 每组 8 个区域。通过与 FreeRTOS MPU 兼容的 API 操作。
 *
 * DPORT_PRO_PMS_C0_REG  ~ DPORT_PRO_PMS_C7_REG   (PRO_CPU)
 * DPORT_APP_PMS_C0_REG  ~ DPORT_APP_PMS_C7_REG   (APP_CPU)
 *
 * 每个区域寄存器格式（32 位）：
 *   bits 31:28 — 保留
 *   bits 27:25 — 访问权限（0=None, 1=R, 2=RW, 3=RX）
 *   bits 24:21 — 保留
 *   bits 20:14 — 区域大小（编码为 2^(size+1)，0 表示禁用）
 *   bits 13:0  — 区域基址[27:14]（32 字节对齐）
 *
 * 由于直接访问 ESP32 DPORT 寄存器需要包含 esp32-hal 特定头文件，
 * 以下实现使用 FreeRTOS MPU API 或 ESP-IDF 等效函数（当可用时）。
 * 当前为框架实现，硬件寄存器操作将在设备上联调时完成。
 */

/* ═══ 内部辅助 ═══ */

/**
 * @brief  按 KERN_MPU_MIN_ALIGN 向上对齐地址
 */
static inline uint32_t mpu_align_up(uint32_t addr)
{
    return (addr + KERN_MPU_MIN_ALIGN - 1) & ~(KERN_MPU_MIN_ALIGN - 1);
}

/**
 * @brief  按 KERN_MPU_MIN_ALIGN 向下对齐地址
 */
static inline uint32_t mpu_align_down(uint32_t addr)
{
    return addr & ~(KERN_MPU_MIN_ALIGN - 1);
}

/* ═══ MPU 初始化 ═══ */

void kern_mpu_init(void)
{
    kern_log(KERN_LOG_INFO, "MPU: subsystem initialized (%d regions, %d-byte align)",
             KERN_MPU_MAX_REGIONS, KERN_MPU_MIN_ALIGN);

    /*
     * 初始化所有区域为默认值（全允许）。
     * 实际实现需要：
     *   for (int i = 0; i < KERN_MPU_MAX_REGIONS; i++) {
     *       // 区域 i: 范围 0x00000000 ~ 0xFFFFFFFF, 读写执行
     *       DPORT_PRO_PMS_C(i) = DPORT_PMS_EN | DPORT_PMS_A_RWX
     *                          | PMS_SIZE(0xFFFFFFFF);
     *   }
     */
}

/* ═══ MPU 区域编码 ═══ */

/**
 * @brief  将访问权限枚举转换为 ESP32 PMS 权限位
 */
static uint32_t mpu_encode_access(kern_mpu_access_t access)
{
    switch (access) {
        case KERN_MPU_ACCESS_NONE: return 0;    /* PMS_A_NONE */
        case KERN_MPU_ACCESS_RO:   return 1;    /* PMS_A_R   */
        case KERN_MPU_ACCESS_RW:   return 2;    /* PMS_A_RW  */
        case KERN_MPU_ACCESS_RX:   return 3;    /* PMS_A_RX  */
        case KERN_MPU_ACCESS_RWX:  return 4;    /* PMS_A_RWX */
        default: return 0;
    }
}

/**
 * @brief  将字节大小编码为 ESP32 PMS 区域大小字段
 * @note   ESP32 要求区域大小为 2^n，最小 32 字节。
 *         返回值 = log2(size) - 1
 *         0 表示区域禁用
 */
static uint32_t mpu_encode_size(size_t size)
{
    if (size < KERN_MPU_MIN_ALIGN) return 0;

    /* 向上取整到 2 的幂 */
    uint32_t power = 32;  /* 最小 32 字节 = 2^5 */
    uint32_t aligned = KERN_MPU_MIN_ALIGN;

    while (aligned < size && power < 0xFFFFFFFF) {
        aligned <<= 1;
        power <<= 1;
    }

    /* ESP32 PMS 编码: 0 表示禁用, 1 表示 64 字节, ... */
    if (aligned >= KERN_MPU_MIN_ALIGN) {
        uint32_t encoded = 0;
        uint32_t s = aligned;
        while (s > KERN_MPU_MIN_ALIGN) {
            s >>= 1;
            encoded++;
        }
        return encoded + 1;  /* 32 字节 = 1 */
    }

    return 0;  /* 禁用 */
}

/* ═══ 栈守卫配置 ═══ */

void kern_mpu_setup_stack_guard(struct kern_task *task,
                                void *stack_base, size_t stack_size)
{
    if (task == NULL || stack_base == NULL || stack_size == 0) return;

    kern_mpu_config_t *cfg = task->mpu_config;
    if (cfg == NULL) return;

    /* 计算栈守卫区域：栈底部 KERN_MPU_GUARD_PAGES 页为不可访问 */
    size_t guard_size = KERN_MPU_GUARD_PAGES * KERN_MPU_MIN_ALIGN;
    if (guard_size > stack_size) {
        guard_size = stack_size;  /* 栈太小，守卫整个栈 */
    }

    uint32_t guard_base = mpu_align_down((uint32_t)(uintptr_t)stack_base);

    if (cfg->region_count < KERN_MPU_MAX_REGIONS) {
        uint8_t idx = cfg->region_count++;
        cfg->regions[idx].base    = (void *)(uintptr_t)guard_base;
        cfg->regions[idx].size    = guard_size;
        cfg->regions[idx].access  = KERN_MPU_ACCESS_NONE;
        cfg->regions[idx].type    = KERN_MPU_TYPE_STACK_GUARD;
        cfg->regions[idx].enabled = true;
    }
}

/* ═══ 添加数据区域 ═══ */

int kern_mpu_add_region(struct kern_task *task,
                        void *base, size_t size, kern_mpu_access_t access)
{
    if (task == NULL || base == NULL || size == 0) {
        return KERN_EINVAL;
    }

    kern_mpu_config_t *cfg = task->mpu_config;
    if (cfg == NULL) {
        return KERN_ERR;
    }

    if (cfg->region_count >= KERN_MPU_MAX_REGIONS) {
        return KERN_ENOSPC;  /* 区域槽位已满 */
    }

    /* 地址必须对齐 */
    if (((uint32_t)(uintptr_t)base & (KERN_MPU_MIN_ALIGN - 1)) != 0) {
        return KERN_EINVAL;  /* 未对齐 */
    }

    uint8_t idx = cfg->region_count++;
    cfg->regions[idx].base    = base;
    cfg->regions[idx].size    = size;
    cfg->regions[idx].access  = access;
    cfg->regions[idx].type    = KERN_MPU_TYPE_TASK_DATA;
    cfg->regions[idx].enabled = true;

    return KERN_OK;
}

/* ═══ 应用 MPU 配置 ═══ */

void kern_mpu_apply(struct kern_task *task)
{
    if (task == NULL) return;

    kern_mpu_config_t *cfg = task->mpu_config;
    if (cfg == NULL) return;

    /*
     * 逐区域写入 MPU 寄存器。
     *
     * 伪代码（ESP32 PRO_CPU）：
     *   for (uint8_t i = 0; i < cfg->region_count; i++) {
     *       kern_mpu_region_t *rgn = &cfg->regions[i];
     *       if (!rgn->enabled) continue;
     *
     *       uint32_t reg_val = ((uint32_t)(uintptr_t)rgn->base & 0x3FFF)
     *                        | (mpu_encode_size(rgn->size) << 14)
     *                        | (mpu_encode_access(rgn->access) << 25);
     *
     *       // 写入对应核的 MPU 区域寄存器
     *       switch (kern_cpu_id()) {
     *           case 0: DPORT_PRO_PMS_C(i) = reg_val; break;
     *           case 1: DPORT_APP_PMS_C(i) = reg_val; break;
     *       }
     *   }
     *
     *   // 刷新 MPU 状态（某些 ESP32 需要读取确认）
     *   asm volatile("memw");
     */
}

#endif /* CONFIG_MPU_ENABLED */
