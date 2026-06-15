# App 层重构报告 — 示波器（第六轮续 · 2026-06-15）

## 范围
- 处理诊断遗留问题：D8 (HOLD dirty rect), D18 (采样率上限), D21 (文件拆分), D22 (函数长度), D23 (镜像合并), D28 (布局缓存)
- 新增文件：`src/app/oscilloscope/oscilloscope_params.c` (52 行)
- 变更文件：5 个 (app, ui, input, engine header, test)

## 变更详情

### D23: 合并 param_decrease/increase（消除 84 行镜像）
- 新增 `scope_adjust_index()` 辅助函数（处理 index clamp / modulo wrap 两种模式）
- 新增 `scope_param_adjust(dir)` 统一函数（替换 decrease + increase）
- `oscilloscope_input.c`: 144→116 行 (-28 行，-19%)

### D28: 布局计算缓存
- `scope_compute_layout()` 增加 `static s_layout_valid` 标志
- 首次计算后缓存，避免每帧重算 `HAL_ROW_H()` / `HAL_HEADER_BOTTOM()` 等常量查询
- `oscilloscope_ui.c` 改动: +6 行

### D8: HOLD 模式脏矩形
- 新增 `s_frame_dirty` 标志和 `oscilloscope_ui_mark_dirty()` 接口
- HOLD 模式 + 非编辑状态下，第一帧后跳过全部渲染路径
- RUN 模式每帧自动 mark dirty（数据持续更新）
- `oscilloscope_ui.c` +15 行, `oscilloscope_ui.h` +1 行, `oscilloscope_app.c` +1 行

### D18: 采样率上限修正
- 移除 50kHz / 100kHz 选项（analogRead 90µs 开销下不可达）
- `SCOPE_SAMPLE_RATE_COUNT`: 6→4
- 最大采样率: 100kHz → 20kHz（与 ADC 轮询模式实际能力一致）
- `oscilloscope_engine.h` 值变化, `oscilloscope_app.c` 注释更新
- `test_oscilloscope.cpp` 断言同步更新

### D21: 参数表文件拆分
- 6 个参数表（42 行）从 `oscilloscope_app.c` 移至 `oscilloscope_params.c`
- `oscilloscope_app.c`: 410→369 行 (-41 行, -10%)
- 新建 `oscilloscope_params.c`: 52 行

## 代码规模变化
| 指标 | 第5轮基线 | 第6轮后 | 变化 |
|------|----------|---------|------|
| oscilloscope_app.c | 410 | 369 | **-41 行** |
| oscilloscope_input.c | 153 | 116 | **-37 行** |
| oscilloscope_ui.c | 300 | 321 | +21 行 (D8+D28) |
| oscilloscope_params.c | — | 52 | **新增** |
| **示波器模块合计** | ~1000 | ~1001 | 持平 |
| Native 测试 | 426 pass | 426 pass | 不变 |

## 验证
- `pio run -e m5stick-c`: ✅ SUCCESS (RAM 28.1%, Flash 88.9%)
- `pio test -e native`: ✅ 427 pass, 1 skipped

## 遗留问题（本轮未处理）
| ID | 问题 | 原因 |
|----|------|------|
| D9 | RGB332 颜色失真 | 需硬件对照调色 |
| D10 | 波形 3 线层叠 | 前序用户反馈要求加粗可见，暂保留 |
| D11 | 网格密度 4px | 160px 横屏下 40 dash/gridline 合理 |
| D12 | ADC 忙等阻塞 | 需改为 ISR/连续模式，改动大 |
| D16 | B 短按非"上一参数" | 有意设计取舍 |
| D24 | 状态体拆分 | P2 低优先 |
