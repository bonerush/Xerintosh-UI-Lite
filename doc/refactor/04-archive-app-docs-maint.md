# 归档报告（第七轮 · 2026-06-16）

## 本轮范围

- **App 层核心重构**：wifi_manager.cpp/flasher_app.cpp 超长文件拆分，app_menu_build() 菜单构建提取，ui_service 生命周期标准化
- **文档体系维护**：链接修复，缺失模块文档创建，索引更新
- **UI 核心层维护**：退出动画函数拆分，弹窗代码去重
- **内核层维护**：FIFO SMP 自旋锁补充，错误码风格统一

## 提交清单

### 新增文件
| 文件 | 行数 | 说明 |
|------|------|------|
| `src/app/wifi/wifi_menu.c` | 122 | 网络菜单构建（提取自 wifi_manager.cpp） |
| `src/app/wifi/wifi_menu.h` | 54 | 网络菜单头文件 |
| `src/app/flasher/flasher_proto.c` | 281 | STK500/ESP32 SLIP 协议解析引擎（提取自 flasher_app.cpp） |
| `src/app/flasher/flasher_proto.h` | 56 | 协议解析头文件 |
| `doc/refactor/*.md` | 6 文件 | 基线/诊断/子阶段/集成/归档报告 |

### 修改文件
| 文件 | 变化 | 说明 |
|------|------|------|
| `src/app/wifi/wifi_manager.cpp` | 706→630 | 提取 wifi_menu 后精简 |
| `src/app/flasher/flasher_app.cpp` | 525→243 | 提取 flasher_proto 后精简 |
| `src/app/app_menu.c` | +19 行 | 提取 build_baud/ 子菜单和 build_settings_items() |
| `src/app/taskmgr/taskmgr_app.c` | 2 行 | ui_service_user_item_init/exit 标准化 |
| `src/app/serial_monitor/sm_app.cpp` | 2 行 | 同上 |
| `src/app/token_usage/tu_app.cpp` | 4 行 | 添加 ui_service.h include + 标准化 |
| `src/app/flasher/flasher_app.cpp` | 2 行 | 同上 |
| `src/kernel/kern_sched_fifo.c` | +22 行 | SMP 自旋锁 |
| `src/kernel/kern_task_lifecycle.c` | 0 行 | return 0→KERN_OK |
| `src/ui/ui_draw_anim.c` | +44 行 | 拆分 3 子函数 |
| `src/ui/ui_draw_widgets.c` | -5 行 | 消除 fallback 重复 |
| `doc/index.md` | +3 行 | 新增 app 条目 |
| `doc/app/index.md` | +2 行 | 新增模块条目 |
| `doc/refactor/README.md` | 更新 | 状态跟踪 |

## 验证结果

| 检查项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 427 tests: 1 skipped, 426 passed |
| RAM 使用 | 28.1% (无变化) |
| Flash 使用 | 88.9% (无变化) |
| 代码规模 | 新增 513 行（wifi_menu + flasher_proto），减少 339 行（源文件精简） |

## 合并建议

- **合并方式**：Fast-forward merge（无冲突概率高，修改集中在 App 层和内核锁）
- **合并后动作**：
  1. 主分支 `pio run -e m5stick-c` 确认编译
  2. 主分支 `pio test -e native` 确认测试
  3. 可选：部署到 M5Stick-C 硬件验证菜单和烧录器功能
