# 重构诊断报告

**项目**：M5Stick-P1  
**分支**：`refactor/2026-06-14-kernel-ui`  
**日期**：2026-06-14  
**基线**：`pio run -e m5stick-c` ✅ / `pio test -e native` ✅（348 cases）

---

## 扫描方法

- 静态扫描范围：`src/kernel/`、`src/ui/`、`src/hal/`、`src/app/`
- 扫描工具：人工模式 + `grep`/`awk`/Python 脚本
- 详细分报告：
  - [内核层诊断](01-diagnosis-kernel.md)
  - [UI 层诊断](01-diagnosis-ui.md)

---

## 优先级定义

- **P0**：会导致崩溃、内存泄漏、硬件损坏
- **P1**：功能正确但可维护性差、重复代码、接口不一致
- **P2**：风格、文档、测试缺失

---

## 问题清单（合并）

| ID | 模块 | 文件 | 问题 | 优先级 | 建议动作 | 关联测试 |
|----|------|------|------|--------|----------|----------|
| K-P0-1 | 内核-任务生命周期 | `kern_port_freertos.c` | 任务自然返回未调用 `kern_exit()`，资源泄漏 | P0 | 在 `task_wrapper()` 返回路径调用 `kern_exit()` | `test_kernel_task` |
| K-P0-2 | 内核-任务生命周期 | `kern_task_lifecycle.c` | Native 蹦床未调用 `kern_exit()` | P0 | 在蹦床末尾调用 `kern_exit()` | `test_kernel_task` |
| K-P0-3 | 内核-任务生命周期 | `kern_task_lifecycle.c` | `reap_zombies()` 未释放 `stack_base` | P0 | 回收 TCB 前释放栈 | `test_kernel_stack` |
| K-P0-4 | 内核-VFS | `kern_vfs.c` | FD 表是全局单表，非每任务隔离 | P0 | 将 FD 表移入 `kern_task_t` | `test_kernel_vfs` |
| K-P0-5 | 内核-DevFS | `kern_devfs.c`/`kern_device.c` | 设备注册双轨 API | P0 | 统一为 `kern_device_register()` | `test_kernel_device` |
| K-P1-1 | 内核-错误码 | 多处 | API 返回 `int` 而非 `kern_err_t` | P1 | 统一返回类型 | 全部 kernel 测试 |
| K-P1-2 | 内核-SMP | `kern_smp.h`/`kern_sched.c` | `g_need_resched` 编译依赖不一致 | P1 | 无条件定义或加 guard | `test_kernel_sched` |
| K-P1-3 | 内核-VFS | `kern_vfs.c` | `unlink` 不检查打开引用 | P1 | 引入引用计数 | `test_kernel_vfs` |
| K-P1-4 | 内核-VFS | `kern_vfs.c` | 替换 inode 时泄漏旧 inode | P1 | 释放旧 inode | `test_kernel_vfs` |
| K-P1-9 | 内核-同步 | `kern_sync.c` | mutex 递归计数缺失；unlock 不检查 owner | P1 | 增加递归计数和 owner 检查 | `test_kernel_sync` |
| U-P0-1 | UI-初始化 | `ui_core.c` | 空根节点未处理，可能崩溃 | P0 | 增加空根防御 | `test_ui_empty_root` |
| U-P0-2 | UI-派发 | `ui_dispatch.c` | 派发表仅覆盖 `enter` | P0 | 扩展为多维生命周期派发表 | `test_ui_dispatch` |
| U-P0-3 | UI-Item | `ui_item_base.c` | switch/slider value 未校验 NULL | P0 | 增加 NULL 校验 | `test_ui_item_null_value` |
| U-P1-1 | UI-Item | `ui_item_list.c` | 存在裸强转 | P1 | 使用 `xerintosh_to_user_item()` | `test_ui_item` |
| U-P1-2 | UI-Widget | `ui_item_popup.c`/`ui_draw_widgets.c` | `popup_compute_height` 重复定义 | P1 | 提取公共模块 | `test_ui_widget_utils` |
| U-P1-3 | UI-Core | `ui_core.c` | `xerintosh_animation()` 命名不精确 | P1 | 重命名为 `xerintosh_ease()` | 已有测试 |
| U-P1-4 | UI-Popup | `ui_item_popup.c` | `xerintosh_push_pop_up()` 超长且用 goto | P1 | 拆分函数；移除 goto | `test_ui_popup` |
| U-P1-5 | UI-Draw | `ui_draw_list.c` | `xerintosh_draw_list_item()` 超长 | P1 | 拆分并走绘制派发表 | `test_ui_draw_list` |
| U-P1-9 | UI-Item | `ui_item_list.c` | 移除子树后选择器悬空 | P1 | 调用安全移动 API | `test_ui_selector_safety` |

---

## 本轮重构排期

| 阶段 | 模块 | 处理的问题 ID |
|------|------|---------------|
| 2.1 | 内核层 | K-P0-1 ~ K-P0-5，K-P1-1、K-P1-2、K-P1-9 |
| 2.2 | HAL 层 | 随内核/UI 必要改动同步 |
| 2.3 | UI 核心层 | U-P0-1 ~ U-P0-3，U-P1-1 ~ U-P1-5、U-P1-9 |
| 2.4 | App 层 | 仅同步 API 变化 |
| 2.5 | 文档体系 | 同步 public header 变化 |

---

## 风险提醒

- K-P0-4（FD 表每任务隔离）会改动 `kern_task_t` 结构，影响 VFS、资源、Shell 等多处。
- U-P0-2（多维派发表）会改变 UI 核心调用方式，需要确保 App 层无内联 switch 残余。
