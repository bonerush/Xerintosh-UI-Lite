# 重构基线报告 — App 层 / 示波器（2026-06-15 第五轮）

## 分支与 Commit
- 分支：`refactor/2025-06-15-app-oscilloscope`
- 起始 commit：`eb62b47`
- 父分支：`main`

## 构建基线
- `pio run -e m5stick-c`：✅ PASS
  - RAM:  28.1% (91928 / 327680 bytes)
  - Flash: 88.9% (1863409 / 2097152 bytes)
- `pio test -e native`：✅ 427 test cases: 1 skipped, 426 passed

## 代码规模（示波器模块）
| 文件 | 行数 | 用途 |
|------|------|------|
| `oscilloscope_app.c` | 391 | ADC 采样、触发、测量引擎 |
| `oscilloscope_ui.c` | 289 | 波形/网格/header/footer 渲染 |
| `oscilloscope_input.c` | 144 | 按钮输入状态机 |
| `oscilloscope_engine.h` | 60 | 参数表与触发引擎 API |
| `oscilloscope_ui.h` | 52 | view_state 结构体 |
| `oscilloscope_internal.h` | 30 | 内部状态与常量 |
| `oscilloscope_input.h` | 18 | 输入处理声明 |
| `oscilloscope.h` | 16 | 公共头 |
| **合计** | **1000** | |

App 层总计：8283 行，示波器占比 ~12%。

## 已知问题（来自代码扫描）
1. `oscilloscope_app.c` 391 行超过 400 行建议上限（390 → 接近阈值）。
2. `scope_update_trigger()` 与 `scope_update_measurements()` 每次循环均遍历满量采样缓冲，可能浪费 CPU。
3. AC 耦合计算的滑动窗口固定 32 个样本，大时基下窗口可能太小。
4. 频率测量使用过零法，复杂波形（谐波）下精度有限。
5. 采样循环中 `delayMicroseconds()` 阻塞 FreeRTOS tick，可能影响系统响应。

## 本轮重构范围
- [ ] App 层 — 示波器代码优化与问题修复
- [ ] 性能分析：采样/渲染耗时测量
- [ ] 输入响应性：按键与采样阻塞的协调
- [ ] 测量精度：AC 耦合窗口自适应
