# 重构归档（第九轮 · 2026-06-17 · App 深度优化 + 内核/UI 维护）

## 执行摘要
本轮重构以 **App 层深度优化**为主，**内核/UI 层日常维护**为辅。共发现 37 项问题，修复 16 项，标记待处理 4 项性能/竞态问题。

## 修改文件清单

### 内核层（6 文件）
| 文件 | 修改摘要 |
|------|---------|
| `src/kernel/kern_smp.h` | 移除 `KERN_CPU_ANY` 重复定义 |
| `src/kernel/kern_vfs.c` | 修正 `fd_pool_alloc()` 注释 + 改进目录打开语义注释 |
| `src/kernel/kern_shell.c` | 删除 `for(;;)` 后不可达的 `kern_close(tty)` |
| `src/kernel/kern_procfs.h` | 更新注释：3→5 个文件 |
| `src/kernel/devices/dev_pwrkey.h` | 更新 API 引用注释 |
| `src/kernel/devices/dev_input0.c` | 统一 include 路径 |

### UI 核心层（3 文件）
| 文件 | 修改摘要 |
|------|---------|
| `src/ui/ui_item_selector.c` | 增加 `parent==NULL` 和 `count==0` 防御性检查（P1） |
| `src/ui/ui_core.c` | 调换刷新顺序消除 1 帧延迟 |
| `src/ui/ui_draw_icons.c` | `_item` 参数添加 `const` |

### App 层（6 文件）
| 文件 | 修改摘要 |
|------|---------|
| `src/app/wifi/wifi_menu.h` | `#pragma once` → 标准 include guards |
| `src/app/token_usage/tu_api.h` | `#pragma once` → 标准 include guards |
| `src/app/app_menu.c` | 统一 include 路径 |
| `src/app/ui_service.c` | 删除 native 测试死代码 |
| `src/app/wifi/wifi_manager.cpp` | 修正过时注释 + 添加跨任务共享缓冲区 TODO |
| `src/app/bluetooth/bt_manager.cpp` | 修正过时注释 |
| `src/app/storage/storage.cpp` | Native 桩返回值与硬件语义一致 |

### 文档（3 文件）
| 文件 | 修改摘要 |
|------|---------|
| `doc/refactor/README.md` | 更新轮次状态 |
| `doc/refactor/00-baseline-app-deep-optimization.md` | 基线报告 |
| `doc/refactor/01-diagnosis-app-deep-optimization.md` | 诊断报告 |
| `doc/refactor/02-refactor/app-deep-optimization.md` | 重构报告 |

## 关键发现

### 验证为误报
- **A0**：`flasher_menu.c` 中 `free()` 释放静态内存 → UI 框架 `xerintosh_init_base_item()` 对所有 content 使用 `strdup()`，始终为堆分配，`free()` 安全。

### 修复的真问题
- **U1/U2**：UI 选择器空指针解引用和数组越界（P1 崩溃风险）
- **A8**：2 个头文件使用 `#pragma once` 违反项目规范
- **A1**：native 测试分支死代码

### 标记待后续处理
- WiFi 弹窗跨任务共享缓冲区竞态
- 蓝牙 TOCTOU 竞态
- 示波器 UI 线程阻塞采样
- 烧录器阻塞 delay

## 验证结果
| 项目 | 结果 |
|------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS（RAM 28.2%, Flash 88.9%） |
| `pio test -e native` | 211 cases / 210 passed（无退化） |
| 编译警告 | 无新增警告 |
| 资源使用 | 无变化 |

## 建议后续行动
1. **后续轮次**：处理标记的 4 个性能/竞态 TODO（需要硬件测试）
2. **同步至 main**：确认后可合并回 main 分支
3. **文档更新**：`doc/coding-style.md` 中强调 include guard 规范
