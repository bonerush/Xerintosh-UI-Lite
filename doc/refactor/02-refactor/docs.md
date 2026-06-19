# 阶段 2.5：文档体系同步报告

> **Parent:** [阶段 2 重构总览](../README.md) | **Related:** [kernel.md](kernel.md), [hal.md](hal.md), [ui.md](ui.md), [app.md](app.md)

## 概述

本轮（Phase 2.5）将 `doc/` 中文档与阶段 2.1–2.4 的源码变更进行同步。重点包括：

- 补齐本轮新增/变更的公共 API 说明；
- 修正因源码行号漂移导致的过期源码链接；
- 删除过时的架构描述（例如任务管理器显示虚任务、旧版 BT 启用接口等）；
- 为此前完全没有文档的 App / HAL / UI 模块新建说明页；
- 更新中央知识地图 `doc/index.md` 与各子目录 `index.md`。

**注意**：本轮仅修改文档，不修改源码。

---

## 同步范围

### App 层（`doc/app/`）

| 文档 | 主要变更 | 优先级 |
|------|----------|--------|
| [settings.md](../../app/settings.md) | 补充弹簧（硬度/阻尼）与动画风格设置；`settings_load_from_storage()` 加载弹簧模式并同步 selector；修正 getter/setter 源码链接；回调表新增 3 项弹簧回调 | P1 |
| [app-menu.md](../../app/app-menu.md) | 更新设置菜单结构图，加入 `动画风格`、`弹动硬度`、`反弹力度`；修正 `build_baud_submenu()` 源码链接 | P1 |
| [app-init.md](../../app/app-init.md) | 补充 `xerintosh_set_dual_key_callback(power_key_popup_is_dual_active)` 说明；依赖表补全 3 个弹簧变更回调 | P1 |
| [taskmgr.md](../../app/taskmgr.md) | 删除“虚任务可在任务管理器中显示/终止”的错误描述；修正 `taskmgr_init` / `taskmgr_loop` / 确认保护逻辑源码链接 | P0 |
| [serial-monitor.md](../../app/serial-monitor.md) | BLE 启用示例改为 `svc_mgr_bt_request_enable(&sm_bt_lazy_inited)`；修正初始化/入场动画/按钮 alpha 源码链接；动画函数统一为 `xerintosh_animate_unified`；新增 `SM_DBG_ENABLED` 调试开关说明 | P0/P1 |
| [flasher.md](../../app/flasher.md) | 修正 RX 噪音过滤与 USB↔UART 透传源码链接；新增 `s_entry_offset` / `xerintosh_animate_unified` 入场动画说明 | P0 |
| [svc-mgr-helper.md](../../app/svc-mgr-helper.md) | 修正 `svc_mgr_helper.h` 源码链接 | P2 |
| [ui-service.md](../../app/ui-service.md) | 修正 `about.c` 生命周期源码链接 | P2 |
| [index.md](../../app/index.md) | 新增 boot、wifi、bluetooth、serial_input、storage、about、app_state 入口 | P1 |

#### 新建 App 文档

- [boot.md](../../app/boot.md) — 开机画面 `boot_screen_draw_logo()` 生命周期
- [about.md](../../app/about.md) — 关于页面布局与版本信息
- [storage.md](../../app/storage.md) — NVS 持久化封装与批量保存/加载
- [app-state.md](../../app/app-state.md) — 跨模块全局状态与变更回调
- [wifi.md](../../app/wifi.md) — WiFi 状态机、扫描/连接、动态网络菜单
- [bluetooth.md](../../app/bluetooth.md) — Classic BT SPP 管理器与 UART 服务
- [serial-input.md](../../app/serial-input.md) — 串口 CLI 输入状态机（WiFi 密码 / BT 配对码）

---

### 内核层（`doc/kernel/`）

| 文档 | 主要变更 | 优先级 |
|------|----------|--------|
| [kern-sched-rr.md](../../kernel/kern-sched-rr.md) | 重写 enqueue/dequeue：O(1) `task_list_tail`、SMP `task_list_lock`、`scheduler_class_id` 同步；修正全部源码链接 | P0 |
| [kern-resource.md](../../kernel/kern-resource.md) | 新增 `RES_POOL_SIZE 32` 对象池说明；重写 track/untrack/release_all 源码块与链接 | P0 |
| [kern-device-model.md](../../kernel/kern-device-model.md) | 重写 `kern_device_register()`：自动创建 `/dev/<name>`、`mutex` 保护、失败回滚；新增 `kern_device_init()` / `kern_device_unregister()`；修正桥接层链接 | P0 |
| [kern-port.md](../../kernel/kern-port.md) | 更新 `kern_port_ops_t`（`preempt_consume`、修正 `timer_set_periodic` 签名）；新增 `kern_port_preempt_consume()`；修正 FreeRTOS 后端链接 | P0 |
| [kern-shell-cmds.md](../../kernel/kern-shell-cmds.md) | 更新命令表链接；补全 `meminfo` / `tree` / `tasks` / `uptime`；修正历史缓冲区大小为 `HISTORY_SIZE 8` / 64 B；修正 `cmd_ps`、`cmd_scope` 链接 | P0 |
| [kern-task.md](../../kernel/kern-task.md) | 修正 Native tick 栈检查、ESP32 spawn 范围、栈使用、port 栈查询链接 | P1 |
| [kern-kmalloc.md](../../kernel/kern-kmalloc.md) | 移除资源节点直接调用 `kern_kmalloc_untracked()` 的过时描述，改为对象池优先/堆回退 | P1 |
| [kern-sched-class.md](../../kernel/kern-sched-class.md) | 补充 `task_list_tail` 字段；修正偏移链接 | P1 |
| [kern-sched-fifo.md](../../kernel/kern-sched-fifo.md) | enqueue 补充 `scheduler_class_id` 同步；dequeue 补充 `task_list_tail`/`-1`；删除过时全局表描述；修正链接 | P1 |
| [kern-vfs.md](../../kernel/kern-vfs.md) | 修正 open/read/close/path_walk/inode ref 偏移链接 | P1 |
| [kern-init.md](../../kernel/kern-init.md) | 修正指向 Native spawn 却描述 ESP32 行为的源码链接 | P1 |
| [kern-sync.md](../../kernel/kern-sync.md) | `mutex_t` 片段加入 `recursive_count`；更新 lock/unlock 代码与伪代码；修正链接 | P1 |

---

### HAL / UI 核心层（`doc/hal/`、`doc/ui/`）

| 文档 | 主要变更 | 优先级 |
|------|----------|--------|
| [hal/display.md](../../hal/display.md) | 新增 `hal_display_clear_color()`；更新 `hal_display_set_rotation()` 精灵重建逻辑；`hal_draw_string()` 增加 `\n` 换行示例；新增 `hal_set_font()` 章节 | P1 |
| [hal/input.md](../../hal/input.md) | `M5.update()` 现在位于 `hal_input_update()` 内；更新双击状态机为“先处理边沿再检查超时”；更新架构图 | P0 |
| [hal/index.md](../../hal/index.md) | 新增 `layout.md` 入口 | P1 |
| [ui/core.md](../../ui/core.md) | 主循环替换为含脏矩形跳过和双键回调的版本；生命周期增加 NULL 检查与 `xerintosh_invalidate()`；渲染顺序改为 list_item → selector → camera；修复源链接漂移 | P0 |
| [ui/context.md](../../ui/context.md) | `exit_requested` 改为 `volatile bool`；新增 Phase 2.3 弹簧参数章节；修复源链接 | P1 |
| [ui/item.md](../../ui/item.md) | 增加 `button_item`、`item_type_count`、子项上限 12；新增 selector API 说明；修复链接 | P1 |
| [ui/dispatch.md](../../ui/dispatch.md) | 重锚全部 `dispatch_enter_*`、vtable、公开派发链接；新增 `xerintosh_selector_go_prev_item` | P1 |
| [ui/drawer.md](../../ui/drawer.md) | 更新列表外观/列表项/选择器/弹窗源码片段与链接 | P1 |
| [ui/draw-list.md](../../ui/draw-list.md) | 同步可见性、列表外观、列表项、选择器、滑块覆盖层、滚动偏移、长按提示的源码片段与链接 | P1 |
| [ui/draw-widgets.md](../../ui/draw-widgets.md) | 更新弹窗片段；拆分信息栏/弹窗结构体链接；新增 Widget 管理 API 章节 | P1 |
| [ui/draw-anim.md](../../ui/draw-anim.md) | 更新主循环交互链接；增加双键模式下跳过退场动画说明 | P1 |
| [ui/index.md](../../ui/index.md) | 新增 selector/camera/widget/types 入口；重构任务列表补充 Phase 2.3 内容 | P1 |

#### 新建 HAL / UI 文档

- [hal/layout.md](../../hal/layout.md) — `hal_layout.h` 三层布局宏体系与使用示例
- [ui/selector.md](../../ui/selector.md) — 选择器结构、绑定、导航、安全移出/锚定重建
- [ui/camera.md](../../ui/camera.md) — 相机/视口结构与滚动算法
- [ui/widget.md](../../ui/widget.md) — 信息栏与弹窗数据模型及生命周期 API
- [ui/types.md](../../ui/types.md) — `ui_types.h` 枚举、回调、动画常量、布局常量

---

## 其他链接修正

- `doc/developer-guide.md`：修正 3 处 `../../src/...` 为 `../src/...`（该文件位于 `doc/` 根目录）。
- `doc/app/serial-monitor.md`：将 `[sm_buffer.c/h]` 拆分为 `[sm_buffer.c]` 与 `[sm_buffer.h]` 两个独立链接。

---

## 中央索引同步

`doc/index.md` 已同步：

- App 层列表新增 boot / about / storage / app_state / wifi / bluetooth / serial_input；
- HAL 层列表新增 layout；
- UI 核心层列表新增 selector / camera / widget / types。

---

## 验证

- [x] `pio run -e m5stick-c` 通过 — RAM 27.0% (88432/327680), Flash 89.3% (1873085/2097152)
- [x] `pio test -e native --filter test_native` — 187 个用例通过，测试框架在 teardown 阶段因全局状态污染触发 `SIGTRAP` 退出，与文档改动无关（历史已知问题）
- [x] 内部相对链接检查：除本轮即将覆盖的 `doc/refactor/02-refactor/docs.md` 外，其余相对链接均可解析到现有文件

---

## 延后项

以下问题在本次文档同步中**未处理**，计划在后续阶段解决：

1. **文件大小超限**：`doc/ui/core.md`（约 574 行）、`doc/ui/item.md`（约 523 行）已超过 400 行上限，本次仅更新内容未拆分。
2. **未审计文档**：`doc/kernel/kern-mpu.md`、`doc/kernel/kern-smp.md` 等仍存在个别偏移链接；`doc/refactor/01-diagnosis-*.md` 为诊断草稿，内部链接未逐一修复。
3. **行号精确校准**：部分历史代码片段的行号仍可能存在 ±1~±3 误差，后续将用脚本统一巡检。

---

## 提交

```bash
git add -A
git commit -m "docs: Phase 2.5 documentation system sync

Co-Authored-By: kimi-k2.6 <MoonshotAI@claude-code-best.win>"
```

---

> **See Also:** [阶段 2 重构总览](../README.md) | [内核重构报告](kernel.md) | [App 重构报告](app.md) | [HAL 重构报告](hal.md) | [UI 重构报告](ui.md)
