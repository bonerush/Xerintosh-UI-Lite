/**
 * @file   kern_procfs.h
 * @brief  Xeros /proc 虚拟文件系统头文件
 * @details 定义 kern_procfs_init() 接口，将内核状态信息以文件形式
 *          挂载到 VFS 的 /proc/ 路径下，遵循 "一切皆文件" 哲学。
 *
 *          目前支持的静态文件：
 *          - /proc/tasks     — 任务列表
 *          - /proc/uptime    — 内核运行时间
 *          - /proc/version   — 内核版本及编译信息
 *          - /proc/meminfo   — 堆内存统计
 *          - /proc/developer — 开发者与项目信息
 *
 * @copyright Copyright (c) 2026
 */

#ifndef KERN_PROCFS_H
#define KERN_PROCFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ procfs 生命周期 ═══ */

/**
 * @brief 初始化 /proc 虚拟文件系统
 * @note  创建 /proc 目录，注册 /proc/tasks、/proc/uptime、
 *         /proc/version 三个只读文件。
 *        需在 VFS 和调度器初始化之后调用。
 */
extern void kern_procfs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KERN_PROCFS_H */
