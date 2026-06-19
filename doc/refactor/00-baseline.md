# 重构基线报告：fullstack（第十轮 · 2026-06-19）

## 分支与 Commit
- 分支：`refactor/2026-06-19-fullstack`
- 起始 commit：`f381d3b9a51824425bfcc069b7e383aa8174832c`
- 父 commit 信息：`fix(wifi): 修复启用 WiFi 后网络菜单不出现`

## 构建基线
- `pio run -e m5stick-c`：✅ PASS
  - RAM:  27.0% (88408 / 327680 bytes)
  - Flash: 89.2% (1870097 / 2097152 bytes)
- `pio test -e native`：⚠️ PASS with known issue
  - test_ble_uart: ✅ PASS (20 tests)
  - test_native: ⚠️ ERRORED (SIGTRAP at teardown — 所有 test case 通过，PlatformIO runner 已知问题)
  - test_token_usage: ✅ PASS (11 tests)
  - 合计: 225 test cases, 224 succeeded

## 代码规模
| 类型 | 数量 | 代码行 |
|------|------|--------|
| C 文件 | 70 | — |
| C++ 文件 | 24 | — |
| 头文件 | 87 | — |
| **src/ 合计** | 181 | **27,318** |
| doc/ markdown | — | **19,311** |

## 已知问题（来自 TODO/FIXME 扫描）

| ID | 文件 | 行 | 内容 | 优先级 |
|----|------|-----|------|--------|
| T1 | `src/kernel/kern_init.c` | 121 | `/* TODO: 硬件 LED 闪烁 */` | P2 |
| T2 | `src/kernel/kern_port_native.c` | 198 | `/* TODO: 使用 esp_timer 或简单的忙等待 */` | P2 |
| T3 | `src/kernel/devices/dev_ttyS0.cpp` | 20 | `/* TODO(phase 2.4): 迁移到 app/flasher/flasher.h 声明 */` | P1 |
| T4 | `src/app/wifi/wifi_manager.cpp` | — | `g_popup_content` 跨任务无锁共享 | P1 |

## 用户反馈问题（本轮核心）

| ID | 问题描述 | 涉及层 | 优先级 | 预判 |
|----|----------|--------|--------|------|
| U1 | 系统日志不能换行显示 | UI/HAL | P0 | serial_monitor sm_ui.c 渲染逻辑 |
| U2 | Shell cat 命令 bug：不能相对路径打开，需要完整地址 | 内核 | P0 | kern_shell_cmds.c cat 实现 |
| U3 | io 指令 GPIO 操作无效（背光控制） | 内核/HAL | P0 | gpiofs/sysfs 与硬件交互断层 |
| U4 | 全 Shell 命令测试 | 内核 | P1 | 系统性测试脚本 |
| U5 | Shell-内核交互整理 | 内核/HAL | P1 | 审计 kern_shell ↔ kern_vfs 链路 |
| U6 | 蓝牙/WiFi 优化（蓝牙严重问题） | App | P0 | bt_manager / wifi_manager 状态机 |
| U7 | 日常维护、debug、优化 | 全栈 | P1 | 贯穿全流程 |

## 本次重构范围
- [x] 内核层 — shell cat/io 命令修复、gpiofs 硬件桥接审计
- [x] UI 核心层 — 日志换行渲染
- [x] HAL 层 — 显示/输入/背光控制
- [x] App 层 — 蓝牙/WiFi 优化
- [x] 文档体系 — 同步更新
