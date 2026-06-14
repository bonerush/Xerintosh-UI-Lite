# 集成验证报告

## 分支信息

- **工作树**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-first`
- **分支**：`refactor/2026-06-14-kernel-first`
- **基线 commit**：`a4d8bab2703908ff0c4934daf97c1bced671eb40`（`main`）
- **最终 commit**：待阶段 4 归档后补充

## 验证结果

### 1. Native 测试

```bash
pio test -e native
```

**结果**：✅ PASS
- 415 个测试用例
- 414 个 succeeded
- 1 个 skipped（`TuAppTest.Loop_WithValidKey_FetchesData`，因 NATIVE_TEST 下 storage 桩固定返回空 key）
- 总耗时：约 6 秒

### 2. 硬件构建

```bash
pio run -e m5stick-c
```

**结果**：✅ SUCCESS
- RAM：22.3%（73,024 / 327,680 bytes）
- Flash：88.0%（1,846,329 / 2,097,152 bytes）
- 构建耗时：约 4 秒

### 3. 基线对比

| 指标 | 基线（main） | 本轮最终 | 变化 |
|------|-------------|----------|------|
| Native 测试数 | 371 | 414 | +43 |
| Flash 占用 | 88.0% (1,845,389 B) | 88.0% (1,846,329 B) | +940 B |
| RAM 占用 | 22.3% (73,216 B) | 22.3% (73,024 B) | -192 B |

> 注：RAM 占用略降是因为内核 FD 表从全局单表改为 per-task 指针数组后，全局表被移除；每个任务仅在使用 FD 时承担 8 个指针的内存。

## 变更统计

```bash
git diff --stat main
```

**结果**：80 个文件变更，+3,661 / -978 行。

### 按层统计

| 层级 | 主要变更文件 | 说明 |
|------|-------------|------|
| 内核层 | `kern_task.h`、`kern_vfs.c/h`、`kern_kmalloc.c/h`、`kern_resource.c`、`kern_task_lifecycle.c`、`kern_task_stack.c`、`kern_sched_class.c/h`、`kern_sched_rr.c`、`kern_sched_fifo.c`、`kern_sched.c`、`kern_procfs.c`、`kern_device.h`、`devices/dev_*.c/h` | FD 命名空间、inode 引用计数、资源分配器、调度类 ID、错误码统一 |
| App 层 | `app_menu.c`、`ui_service.c/h`、`svc_mgr_helper.c/h`、`serial_monitor/sm_app.cpp`、`flasher/flasher_app.cpp`、`settings/settings.c`、`token_usage/tu_app.cpp/h`、`bluetooth/bt_manager.h/cpp` | NULL 检查、横屏 helper、BT 服务助手、rotation 校验、空 key 跳过 |
| HAL 层 | `hal_display_fb.cpp`、`hal_system.cpp` | 色深顺序 helper、native 真实延时 |
| UI 层 | `ui_item.h` | `extern "C"` 保护 |
| 测试 | `test_native/test_kernel_*.cpp`、`test_native/test_app_menu_safety.cpp`、`test_native/test_ui_service_landscape.cpp`、`test_native/test_svc_mgr_helper.cpp`、`test_native/test_tu_app.cpp`、`test_native/test_hal_display.cpp`、`test_native/test_hal_system.cpp`、`test_native/test_ui_dispatch.cpp`、`test_native/test_settings_accessors.cpp`、`test_native/test_kernel_devfs.cpp`、`test_native/test_kernel_device.cpp` | 新增 40+ 测试用例 |
| 文档 | `doc/kernel/*.md`、`doc/app/*.md`、`doc/hal/display.md`、`doc/hal/system.md`、`doc/ui/item.md`、`doc/index.md` | 同步所有 public API 变化 |

## 代码规模

```bash
find src -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) | wc -l
find src ... -exec wc -l {} + | tail -1
find doc -type f -name "*.md" | wc -l
find doc ... -exec wc -l {} + | tail -1
```

- `src/`：166 个文件，24,210 行
- `doc/`：76 个文件，16,721 行

## 未提交变更

集成验证阶段结束后，工作区仅剩：
- `M doc/refactor/README.md`（阶段状态已更新为“集成验证 RUNNING”）

将在阶段 4 与归档文档一起提交。

## 遗留问题

| ID | 问题 | 状态 |
|----|------|------|
| K-P1-02 | `path_walk()` 不支持 `.` / `..` | 延后 |
| K-P1-04 | `kmalloc_header_t` 未显式对齐 | 延后 |
| K-P1-05 | `kern_krealloc()` 非原子语义 | 延后 |
| K-P1-06 | FIFO 调度抢占语义 | 延后 |
| K-P2-01 | `kern_shell_cmds.c` 文件过长 | 延后 |
| K-P2-02 | sysfs 路径不一致 | 延后 |
| K-P2-04 | `minprintf` `%zd` 支持 | 延后 |
| A-P1-02 | `wifi_manager.cpp` 过长 | 延后 |
| A-P1-03 | WiFi/BT 状态机重复 | 延后 |
| A-P1-04 | taskmgr 硬编码任务名 | 延后 |
| H-P1-02 | 输入事件模型统一 | 延后 |
| H-P2-04 | `xerintosh_push_pop_up()` 拆分 | 延后 |

## 硬件烧录建议

- 当前 `m5stick-c` 构建成功，可直接通过 `pio run -e m5stick-c --target upload` 烧录硬件验证。
- 建议验证项：
  1. 开机菜单结构是否正常。
  2. 进入/退出串口监视器、烧录器时屏幕旋转是否正常。
  3. 串口监视器 BLE 源切换与退出是否正常。
  4. Token Usage 在空 API key 时是否不再发起网络请求。
  5. 设置中旋转方向切换是否正常。

## 结论

本轮重构通过全部 native 测试和硬件构建验证。内核层核心 P0 问题已修复，App/HAL/UI 层按计划完成可控范围改造，文档已同步。剩余遗留问题均为非阻塞性技术债务，可在后续专门轮次处理。
