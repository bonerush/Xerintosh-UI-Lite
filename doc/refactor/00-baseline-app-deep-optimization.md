# 重构基线报告（第九轮 · 2026-06-17 · App 深度优化 + 内核/UI 日常维护）

## 分支与 Commit
- 分支：`refactor/2026-06-17-app`
- 起始 commit：`058efeccba666c0b42b1e34eb48fa9721e859a06`
- 工作区：`.worktrees/refactor-2026-06-17-app`

## 构建基线
- `pio run -e m5stick-c`：✅ PASS（RAM 28.2%, Flash 88.9%）
- `pio run -e native`：✅ PASS
- `pio test -e native`：
  - `test_ble_uart`：✅ PASS（194 tests）
  - `test_native`：⚠️ ERRORED（SIGTRAP at teardown，所有 test cases 通过，PlatformIO runner 已知问题）
  - `test_token_usage`：✅ PASS（6 tests）
  - 总计：211 test cases，210 succeeded

## 代码规模
| 指标 | 数值 |
|------|------|
| 源文件数 | 181 |
| C/C++ 头文件 | 含在 181 内 |
| 总行数 | ~26,379 |
| 测试文件数 | 47 |

## 已知问题（TODO/FIXME 扫描）
| # | 文件 | 行号 | 内容 |
|---|------|------|------|
| 1 | `src/kernel/kern_init.c` | 121 | `/* TODO: 硬件 LED 闪烁 */` |
| 2 | `src/kernel/kern_port_native.c` | 198 | `/* TODO: 使用 esp_timer 或简单的忙等待 */` |
| 3 | `src/kernel/devices/dev_ttyS0.cpp` | 20 | `/* TODO(phase 2.4): 迁移到 app/flasher/flasher.h 声明 */` |

## 本次重构范围
- [x] **App 层（重点）**：排查违规操作、性能问题、潜伏 Bug
- [x] **内核层（日常维护）**：代码清理、接口一致性
- [x] **UI 核心层（日常维护）**：边界条件、代码清理
- [ ] HAL 层（本轮跳过）
- [ ] 文档体系（阶段 4 同步）
