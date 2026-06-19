# 诊断报告：fullstack（第十轮 · 2026-06-19）

## 方法
- 静态扫描范围：`src/kernel/`、`src/ui/`、`src/hal/`、`src/app/`
- 扫描日期：2026-06-19
- 扫描方式：3 个并行 explore agent 分模块深度诊断

## 优先级定义
- **P0**：会导致崩溃、功能完全不可用、数据损坏
- **P1**：功能正确但可维护性差、竞态条件、接口不一致
- **P2**：风格、文档、测试缺失、非关键不一致

---

## 问题清单

### 内核/Shell 层

| ID | 问题 | 文件 | 行号 | 优先级 | 建议动作 |
|----|------|------|------|--------|----------|
| D1 | **cat/cp/rm/mkdir/touch/echo>/hexdump 不支持相对路径** — `path_walk()` 要求路径以 `/` 开头，7 个命令跳过 `resolve_path()` | `kern_vfs.c` | 111 | P0 | 在 `path_walk()` 中或在各命令调用前增加相对→绝对路径转换 |
| D2 | **io 命令对背光的操作与 HAL 层独立** — gpiofs 通过 `digitalWrite()` 操作 GPIO26，M5.Display.setBrightness() 走 PWM 通道，两条路径互不知情 | `kern_gpiofs.c`, `hal_display_fb.cpp` | — | P1 | 统一背光控制为一套接口，消除双路径 |
| D3 | **`param save/load` 是存根** — 打印 "NYI" 后返回 | `kern_shell_cmds.c` | — | P2 | 实现或移除 |
| D4 | **Shell 与真实硬件交互审计** — VFS→device bridge→硬件调用链存在，gpiofs 确实调用 `digitalWrite()`，但 sysfs 到背光 PWM 的链路需确认 | `kern_sysfs.c`, `kern_gpiofs.c` | — | P1 | 审计 sysfs 背光节点是否有效触发 PWM 变更 |

### UI/渲染层

| ID | 问题 | 文件 | 行号 | 优先级 | 建议动作 |
|----|------|------|------|--------|----------|
| D5 | **`hal_draw_string()` 不处理 `\n` 换行符** — Native 路径将 `\n` 绘制为空字模后光标右移；M5GFX 路径 `drawString` 为单行 API | `hal_display_font.cpp` | 195-205, 251-256 | P0 | 在 `hal_draw_string()` 中增加 `\n` 换行处理（防御性） |
| D6 | **终端 `\n` 分割代码为死代码** — `draw_terminal()` 中 `strchr(segment, '\n')` 永远返回 NULL（摄入层已剥离 `\n`） | `sm_ui.c` | 208-226 | P2 | 清理死代码，或改为防御性保留 |
| D7 | **日志行长度限制 64 字符** — 超长行被 `strncpy` 截断 | `sm_buffer.h` | 21 | P2 | 可配置化或增加到合理值 |
| D8 | **横屏仅 ~7 行可见** — 20 行 buffer 需要手动滚动，用户可能误以为"没显示" | `sm_ui.c` | 154-228 | P2 | 添加滚动提示或自动滚动到底部 |

### 蓝牙/WiFi 层

| ID | 问题 | 文件 | 行号 | 优先级 | 建议动作 |
|----|------|------|------|--------|----------|
| D9 | **设置菜单缺少蓝牙开关项** — `build_settings_items()` 只创建了WiFi开关，无蓝牙开关，用户无法从UI启停蓝牙 | `app_menu.c` | 74-111 | P0 | 添加蓝牙 switch_item，绑定 `g_bt_on` |
| D10 | **taskmgr 从 UI 任务直接调用同步 `bt_mgr_disable()`** — 违反"仅主任务可调用同步接口"的约束，可能导致 Bluedroid 死锁 → TWDT 复位 | `taskmgr_app.c` | 214 | P0 | 替换为 `bt_mgr_request_disable()` |
| D11 | **`g_popup_content` 跨任务无锁共享** — WiFi 任务写入、UI 任务读取，`strncpy` 非原子 | `wifi_manager.cpp` | 93-142 | P0 | FreeRTOS 队列或 mutex 保护 |
| D12 | **`bt_uart_service_deinit()` TOCTOU 竞态** — `delay(100)` 不能保证 poll 任务已退出 | `bt_uart_service.cpp` | 270-305 | P1 | 任务通知/信号量替代 delay |
| D13 | **BT/WiFi 共存保护是单向的** — BT启用时会关闭WiFi，但WiFi开关不检查BT状态 | `wifi_manager.cpp`, `bt_manager.cpp` | 307-315, 16-36 | P2 | 添加双向互斥检查 |

### 日常维护

| ID | 问题 | 文件 | 行号 | 优先级 | 建议动作 |
|----|------|------|------|--------|----------|
| D14 | `extern bool g_wifi_on`/`g_bt_on` 手写代替 `#include "app_state.h"` | `wifi_manager.cpp`, `bt_manager.cpp` | 49, 77 | P3 | 统一为 include |
| D15 | `g_wifi_on` 默认 true 但注释说 false | `app_state.c` | 12 | P3 | 修正注释 |
| D16 | 冗余 `#include "bt_manager.h"` 在 `app_menu.c` | `app_menu.c` | 18 | P3 | 移除 |

---

## 本轮重构排期

| 子阶段 | 模块 | 处理的问题 ID |
|--------|------|---------------|
| 2.1 | 内核 | D1, D2, D3, D4 |
| 2.2 | HAL | D5 (hal_draw_string \n) |
| 2.3 | UI | D6, D7, D8 |
| 2.4 | App | D9, D10, D11, D12, D13, D14, D15, D16 |
| 2.5 | 文档 | 同步所有变更 |

---

## Shell 命令清单（35 个注册命令）

| 命令 | 功能 | 状态 |
|------|------|------|
| `help` | 显示帮助 | ✅ |
| `clear` | 清屏 | ✅ |
| `echo` | 输出文本 | ✅ |
| `cat` | 读取文件 | ⚠️ 不支持相对路径 (D1) |
| `ls` | 列出目录 | ✅ |
| `cp` | 复制文件 | ⚠️ 不支持相对路径 (D1) |
| `rm` | 删除文件 | ⚠️ 不支持相对路径 (D1) |
| `mv` | 移动/重命名 | ✅ |
| `mkdir` | 创建目录 | ⚠️ 不支持相对路径 (D1) |
| `touch` | 创建空文件 | ⚠️ 不支持相对路径 (D1) |
| `pwd` | 当前目录 | ✅ |
| `cd` | 切换目录 | ✅ |
| `ps` | 任务列表 | ✅ |
| `free` | 内存信息 | ✅ |
| `df` | 磁盘信息 | ✅ |
| `dmesg` | 内核日志 | ✅ |
| `uptime` | 运行时间 | ✅ |
| `version` | 版本信息 | ✅ |
| `reboot` | 重启 | ✅ |
| `shutdown` | 关机 | ✅ |
| `io` | GPIO 读写 | ⚠️ 与 HAL 背光路径独立 (D2) |
| `param` | 参数管理 | ⚠️ save/load 存根 (D3) |
| `hexdump` | 十六进制转储 | ⚠️ 不支持相对路径 (D1) |
| `tree` | 目录树 | ✅ |
| `stat` | 文件状态 | ✅ |
| `meminfo` | 内存详情 | ✅ |
| `uname` | 系统信息 | ✅ |
| `exec` | 执行程序 | ✅ |
| `kill` | 终止任务 | ✅ |
| `nice` | 调整优先级 | ✅ |
| `taskset` | CPU 亲和性 | ✅ |
| `watch` | 周期性执行 | ✅ |
