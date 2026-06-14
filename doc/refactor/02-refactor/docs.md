# 阶段 2.5：文档体系同步报告

> **Parent:** [阶段 2 重构总览](README.md) | **Related:** [kernel.md](kernel.md), [hal.md](hal.md), [ui.md](ui.md), [app.md](app.md)

## 概述

本轮（phase 2.5）为内核层、App 层、HAL 层、UI 核心层 public API 变更同步文档。目标是确保 `doc/` 中文档与源码一致：新增 API 有说明、过时描述已删除、代码片段链接指向当前实现。

**注意**：本轮仅修改文档，不修改源码。

---

## 同步范围

### 内核层（`doc/kernel/`）

| 文档 | 变更内容 | 关键源链接 |
|------|----------|-----------|
| `kern-task.md` | TCB 新增 `fd_table[]`、`resource_lock`；`scheduler_class_id` 初始化语义更新；EXIT 阶段补充 `stack_base = NULL`；删除全局 `g_fd_table` 描述 | [kern_task.h](../../src/kernel/kern_task.h#L86-L90), [kern_task_lifecycle.c](../../src/kernel/kern_task_lifecycle.c#L47-L66) |
| `kern-vfs.md` | 删除全局 FD 表描述，改为“每任务独立的 `task->fd_table[]`”；新增 inode 引用计数章节与测试 helper | [kern_vfs.h](../../src/kernel/kern_vfs.h#L51-L56), [kern_vfs.c](../../src/kernel/kern_vfs.c#L42-L68) |
| `kern-kmalloc.md` | 新增 `kern_kmalloc_for_task()`、`kern_kmalloc_untracked()`、`kern_kfree_untracked()` 说明 | [kern_kmalloc.h](../../src/kernel/kern_kmalloc.h#L40-L67), [kern_kmalloc.c](../../src/kernel/kern_kmalloc.c#L88-L137) |
| `kern-resource.md` | 资源节点改用 `kern_kmalloc_untracked()`；链表操作加 `resource_lock` 保护 | [kern_resource.c](../../src/kernel/kern_resource.c#L14-L107) |
| `kern-sched-class.md` | `kern_sched_class_register()` 返回 `kern_err_t`；`class_id` 字段说明；enqueue/dequeue 同步 `scheduler_class_id` | [kern_sched_class.h](../../src/kernel/kern_sched_class.h#L30-L65), [kern_sched_class.c](../../src/kernel/kern_sched_class.c#L17-L28) |
| `kern-device-model.md` | 设备 ops 与 `kern_device_register()` 返回类型改为 `kern_err_t`；更新 VFS 桥接调用链中“查全局表”为“查当前任务 FD 表” | [kern_device.h](../../src/kernel/kern_device.h#L36-L65) |

### App 层（`doc/app/`）

| 文档 | 变更内容 | 关键源链接 |
|------|----------|-----------|
| `ui-service.md` | 新增 `ui_service_enter_landscape()` / `ui_service_exit_landscape()` 横屏 helper 说明 | [ui_service.h](../../src/app/ui_service.h#L44-L50), [ui_service.c](../../src/app/ui_service.c#L35-L70) |
| `svc-mgr-helper.md` | 整篇重写：模块由“已移除”改为“BT 懒加载助手”，说明 `svc_mgr_bt_request_enable/disable()` | [svc_mgr_helper.h](../../src/app/svc_mgr_helper.h#L24-L35), [svc_mgr_helper.c](../../src/app/svc_mgr_helper.c#L14-L36) |
| `settings.md` | 补充 `settings_set_rotation()` 输入校验：非法值回退到横屏 | [settings.c](../../src/app/settings/settings.c#L97-L103) |
| `token-usage.md` | **新建文档**：Token Usage App 生命周期、`token_usage_get_data()` 测试 getter、空 key 跳过网络请求 | [tu_app.h](../../src/app/token_usage/tu_app.h#L22-L26), [tu_app.cpp](../../src/app/token_usage/tu_app.cpp#L26-L75) |
| `app-init.md` | 补充 `app_menu_push_checked()` 对根节点/子项的 NULL 检查说明 | [app_menu.c](../../src/app/app_menu.c#L39-L52) |
| `app-menu.md` | 新增 `app_menu_push_checked()` 核心 API 说明与伪代码 | [app_menu.c](../../src/app/app_menu.c#L39-L52) |
| `app/index.md` | 更新模块列表：Token Usage、服务管理助手新职责、UI Service 增加横屏 helper | — |

### HAL / UI / 索引

| 文档 | 变更内容 | 关键源链接 |
|------|----------|-----------|
| `hal/display.md` | 更新 `hal_display_create_sprite()` helper 源链接 | [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L84-L105) |
| `ui/item.md` | 补充 `ui_item.h` 使用 `extern "C"` 包裹的说明 | [ui_item.h](../../src/ui/ui_item.h#L15-L17) |
| `doc/index.md` | 更新 App 层模块树与文档树：Token Usage 文档化、服务管理助手改为懒加载助手、UI Service 增加横屏 helper | — |

---

## 新建文件

- `doc/app/token-usage.md` — Token Usage App 文档

## 删除/重写文件

- `doc/app/svc-mgr-helper.md` — 整篇由“模块已移除”重写为“BT 懒加载助手”

---

## 延后项

以下文档/内容本轮未同步，计划在后续阶段处理：

1. **`doc/kernel/kern-sched-rr.md` / `kern-sched-fifo.md`**
   - 变更摘要中 RR/FIFO 的 `enqueue`/`dequeue` 会同步 `task->scheduler_class_id`，但 `kern-sched-class.md` 已覆盖该语义。两个具体类的文档未逐行更新，因为当前文档仍可正确描述其调度行为。
2. **`doc/kernel/kern-procfs-sysfs.md`**
   - `procfs_register_file()` 返回 `kern_err_t` 属于内部实现细节，public API（`kern_procfs_init()`）签名未变，未单独同步。
3. **源码行号精确校准**
   - 部分历史代码片段的行号链接可能因源码行号漂移而存在 ±1~±3 误差。已校准本轮新增/修改的关键链接，剩余链接将在后续文档巡检中统一用脚本校验。
4. **`doc/app/flasher.md` / `serial-monitor.md` 等 App 文档**
   - 本轮未涉及这些 App 的 public API 变更，未做改动。

---

## 验证

- [x] `pio test -e native` 通过 — 414 succeeded / 1 skipped
- [x] `pio run -e m5stick-c` 通过 — RAM 22.3% (73024/327680), Flash 88.0% (1846329/2097152)

验证在本轮文档提交前执行，确认仅修改文档不影响构建。

---

## 提交

```bash
git add -A
git commit -m "docs: sync documentation with kernel/app/hal/ui refactor

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

> **See Also:** [阶段 2 重构总览](README.md) | [内核重构报告](kernel.md) | [App 重构报告](app.md) | [HAL 重构报告](hal.md) | [UI 重构报告](ui.md)
