# Phase 4 归档报告

> **Parent:** [重构跟踪](README.md) | **Prev:** [Phase 3 集成验证](03-integration.md)

## 1. 重构摘要

本次重构围绕 M5Stick-P1 的 **Xeros 内核层**、**HAL 层**、**Xerintosh UI 核心层** 与 **App 层** 展开，目标是在不破坏公共 API 的前提下，解决职责耦合、消除内联类型分支、补齐硬件抽象、收敛全局状态、补齐边界测试与文档。

- **分支**：`refactor/2026-06-14-kernel-ui`
- **基线 commit**：`6411c578124f1bf029169ffe1f5dfbef9fddeee5`
- **完成日期**：2026-06-14
- **总提交数**：11 个
- **总改动**：113 个文件，`+6348 / -2102` 行

## 2. 各阶段产出

| 阶段 | 状态 | 关键产出 |
|------|------|----------|
| 0. 基线建立 | DONE | `00-baseline.md` |
| 1. 扫描诊断 | DONE | `01-diagnosis.md`、`01-diagnosis-kernel.md`、`01-diagnosis-ui.md` |
| 2.1 内核层重构 | DONE | 统一设备模型（`kern_device.c/h`、`kern_devices.c/h`）、统一错误码、调度类与任务退出强化 |
| 2.2 HAL 层重构 | DONE | 拆分 `hal_display.cpp`、新增 `hal_screen.h/c`、补齐旋转/亮度 HAL API、`M5.Display` 直接调用清零 |
| 2.3 UI 核心层重构 | DONE | 完整生命周期 vtable（`ui_dispatch.c`）、空根 NULL 保护、动画/绘制缓存优化 |
| 2.4 App 层重构 | DONE | `app_state.c/h`、`app_menu.c/h`、`app_input.c/h`、`ui_service.c/h`、`flasher_menu.c/h` |
| 2.5 文档同步 | DONE | 更新 `CLAUDE.md`、知识地图、开发者指南、教程与新增模块文档 |
| 3. 集成验证 | DONE | `03-integration.md`：native 371/371，硬件编译成功，无新增 warning |

## 3. 主要代码变更

### 3.1 内核层（Phase 2.1）

- 统一设备注册入口 `kern_device_register()`，迁移 `/dev/fb0`、`/dev/input0`、`/dev/ttyS0` 到设备模型 v2。
- 引入 `kern_err_t` 统一错误码，替换部分 `int` 返回。
- 强化任务退出资源回收：退出路径始终调用 `kern_exit()`。
- 调度类与 SMP 相关头文件语义澄清。

### 3.2 HAL 层（Phase 2.2）

- 拆分 `src/hal/hal_display.cpp` 为四个职责清晰的文件：
  - `hal_display_fb.cpp`：帧缓冲/画布生命周期、方向与亮度配置
  - `hal_display_draw.cpp`：绘制原语（像素、线、矩形、圆、圆角矩形）
  - `hal_display_font.cpp`：字体与文本；native 环境实现 6×8 ASCII 位图字体模拟
  - `hal_display_adv.cpp`：XOR 反色矩形、XBM 位图、裁剪矩形、`hal_test_fb_read` 测试钩子
- 新增 `hal_display_set_rotation/get_rotation/set_brightness/get_brightness` API，迁移 `main.cpp`、`flasher_app.cpp`、`sm_app.cpp`、`dev_fb0.c` 中的 `M5.Display` 直接调用。
- 新增 `src/hal/hal_screen.h/c`，集中定义运行时屏幕尺寸 `g_screen_width` / `g_screen_height`，解耦布局模块与完整显示头文件。

### 3.3 UI 核心层（Phase 2.3）

- `ui_dispatch.c`：实现覆盖 `enter / input_next / input_prev / input_exit / measure / draw / draw_overlay / destroy / has_right_control` 的完整 vtable，替代所有内联 `switch(type)` 分支。
- `ui_core.c`/`ui_item_selector.c`/`ui_draw_list.c`：迁移到派发表，并增加空根保护。
- 列表可见性判断 `xerintosh_is_item_visible()` 公开化。
- 滚动条长度缓存、字体缓存修复、选择器宽度缓存等微优化。

### 3.4 App 层（Phase 2.4）

- 新增 `app_state.c/h`：收敛 `g_wifi_on`、`g_bt_on` 与设置变更回调声明。
- 拆分 `app_init.c`：
  - `app_menu.c/h`：菜单树构建
  - `app_input.c/h`：每帧输入路由与状态机调度
  - `app_init.c`：保留向后兼容的入口封装
- 新增 `ui_service.c/h`：统一 user_item 生命周期辅助（已迁移 `about.c`）。
- 新增 `flasher_menu.c/h`：将烧录器引脚配置 UI 与强制解除状态机迁回 flasher 模块。
- 解耦 `settings.c` 与 `flasher_gpio.h`，迁移角色标签函数。
- 修复 `ui_task.c` 直接调用 Arduino `delay()` 的问题。

### 3.5 测试

- 新增 5 个测试文件：
  - `test_ui_empty_root.cpp`（空根安全）
  - `test_ui_dispatch.cpp`（vtable 生命周期）
  - `test_ui_item_null_value.cpp`（空值保护）
  - `test_app_state.cpp`（全局状态默认）
  - `test_flasher_menu.cpp`（角色标签与引脚配置）
- 更新内核测试套件以匹配设备模型 v2 与错误码变更。
- 测试总数从 354 提升到 **371**。

## 4. 验证结果

| 检查项 | 结果 |
|--------|------|
| `pio test -e native` | **371/371 passed** |
| `pio run -e m5stick-c` | **SUCCESS** |
| Flash 占用 | 88.0% (1,845,325 / 2,097,152 bytes) |
| RAM 占用 | 22.3% (73,216 / 327,680 bytes) |
| 新增 warning/error | 无 |
| 公共 API 破坏性变更 | 无 |

## 5. 风险与后续建议

### 5.1 已知风险

- `app_init.c` 的薄封装模式虽然保留了 `app_init_ui()`/`app_init_managers()`/`app_input_process()` 入口，但 `app_init.h` 目前已通过 `#include` 引入 `app_menu.h` 与 `app_input.h`。如果未来需要进一步隐藏实现，可考虑将 `app_menu.h`/`app_input.h` 从公共头中移除，仅保留 `app_init.h` 作为唯一入口。
- `ui_service.c` 当前只迁移了 `about.c`。其余 `user_item` App（`serial_monitor`、`taskmgr`、`token_usage`、`flasher`）仍使用自己的输入重置/退出检测逻辑。建议后续逐步迁移，以进一步统一生命周期。
- `flasher_menu.c` 的强制解除状态机逻辑与重构前保持一致，但该状态机依赖 `g_xerintosh_selector` 等全局状态，未来可考虑将其进一步封装为独立状态对象。

### 5.2 后续建议

1. **继续迁移 user_item App 到 ui_service**：优先迁移 `taskmgr` 与 `about` 之后最常用的 `serial_monitor`。
2. **补充 E2E/冒烟测试**：当前 native 测试集中在单元级别，建议增加菜单导航路径、烧录器引脚配置流程的集成测试。
3. **HAL 层进一步解耦**：`hal_layout.h` 仍为 `hal_get_font_height()` 依赖 `hal_display.h`，若未来需要更彻底的隔离，可将字体高度前向声明移入布局头文件。
4. **内存优化**：Flash 已使用 88%，未来新增功能需关注空间余量。
5. **文档自动化**：考虑在 CI 中增加文档链接有效性检查，避免源链接因行号变化而失效。

## 6. 归档清单

- [x] 所有阶段报告已写入 `doc/refactor/02-refactor/` 与 `03-integration.md`
- [x] `doc/refactor/README.md` 已更新为最终状态
- [x] 代码、测试、文档均已提交到分支
- [x] native 与硬件编译验证通过
- [ ] 未 push（按用户要求，暂不 push）

## 7. 如何继续

如需合并到主分支，可执行：

```bash
cd /Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-ui
git push origin refactor/2026-06-14-kernel-ui
```

然后创建 Pull Request，附上本报告与 `doc/refactor/README.md` 中的阶段摘要。
