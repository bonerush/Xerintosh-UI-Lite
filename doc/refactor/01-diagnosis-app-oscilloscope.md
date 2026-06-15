# 诊断报告 — App 层 / 示波器（2026-06-15 第五轮）

## 方法
- 静态扫描范围：`src/app/oscilloscope/`（8 文件，1000 行）
- 扫描方式：3 个 explore agent 并行深度分析
  - Agent 1：引擎层（采样/触发/测量/AC耦合/全局状态）
  - Agent 2：UI 渲染层（管线/布局/波形/网格/颜色）
  - Agent 3：输入处理层（状态机/按键映射/App 一致性）
- 扫描日期：2026-06-15

## 优先级定义
- **P0**：崩溃、数据损坏、功能完全不可用
- **P1**：功能可用但体验差、性能问题、界面不一致
- **P2**：代码风格、可维护性、测试覆盖

---

## 问题清单

### P0 — 致命问题（7 项）

| ID | 模块 | 文件 | 问题 | 影响 |
|----|------|------|------|------|
| D1 | 触发-显示 | `oscilloscope_app.c:377` `oscilloscope_ui.c:86` | **trigger_index ≥ sample_count 时波形完全空白**。`scope_update_trigger()` 在 `SCOPE_SAMPLE_MAX=2000` 中搜索触发，但 `sample_count` 只记录当前帧采集的新样本数。当 trigger_index > sample_count 时，`scope_draw_wave` 中 `idx >= sample_count`→break，0 个点被绘制 | 任意非默认时基组合即触发——最常见的用户可见 bug |
| D2 | AC 耦合 | `oscilloscope_app.c:236-237` | **AC 耦合负值截断为 0（半波整流）**。相减后 `v < 0 → v = 0`，信号下半周期被丢弃。显示波形推至上方、底部削平；Vpp 测量偏小约 50% | 静默数据损坏，用户无法直观辨别 |
| D3 | 像素索引 | `oscilloscope_ui.c:100-103` | **环形缓冲区尾部映射导致最后 2px 列成为"死区"**。`idx = start + px*window/WIDTH`，当 start>0 时，右边的 `idx` 提前触达 sample_count → break；最右侧像素永远不会被绘制 | start 越大死区越大 |
| D4 | AC 偏移 | `oscilloscope_app.c:198-214` | **AC 偏移窗口固定 32 样本不随信号周期缩放**。在 5ms/div + 50Hz 下仅覆盖 0.16% 的周期 → DC 偏移估算为随机值 → 波形垂直方向剧烈抖动 | 低频/大时基下 AC 耦合不可用 |
| D5 | 频率测量 | `oscilloscope_app.c:302-308` | **过零法无滞回，对噪声敏感**。每个噪声峰谷产生额外过零计数。1kHz + 50mV 噪声 → 读数在 1-5kHz 跳动 | 频率值不可靠 |
| D6 | 顶部值丢失 | `oscilloscope_ui.c:44-46` | **`raw >= full_scale` 时 clamp 为 `full_scale-1`**，丢失最高动态范围。满幅信号缺失屏幕顶行像素 | 视觉上 3.3V 满幅显示不到顶 |
| D7 | AC 耦合测量 | 同上 AC 耦合路径 | `scope_update_measurements()` 在 AC 模式使用截断后的数据，Vpp/Vavg 均偏小约 50% | 与 D2 相关联 |

### P1 — 重要问题（12 项）

| ID | 模块 | 文件 | 问题 | 影响 |
|----|------|------|------|------|
| D8 | 渲染性能 | `oscilloscope_ui.c:281-289` | **每帧全量重绘，HOLD 模式未用 dirty rect**。每帧 ~600 次绘制原语。框架已有 `ui_dirty.h`，示波器未集成 | HOLD 模式浪费 60fps×600=36000 调用/秒 |
| D9 | 颜色失真 | `oscilloscope_ui.c:13-19` | **8-bit RGB332 帧缓冲上 RGB565 颜色常量失真**。`SCOPE_COL_WAVE_DIM` 在 RGB332 下近乎不可见；`SCOPE_COL_GRID` 显示为青色而非灰蓝 | 轮廓线和网格颜色与设计不符 |
| D10 | 过度绘制 | `oscilloscope_ui.c:110-112` | **波形 3 线层叠（轮廓+主线+峰值点）**，每帧 240 次线绘制。横屏时 wave_h 仅 ~30px，主线已够 | 不必要的 GPU 负载 |
| D11 | 网格密度 | `oscilloscope_ui.c:62-75` | **4px 间距虚线在 80px 屏幕上视觉接近连续线**。260 像素点/帧 | 可缩减到 8px 间距，视觉几乎相同 |
| D12 | 采样阻塞 | `oscilloscope_app.c:371-376` | **`analogRead` + `delayMicroseconds` 忙等阻塞 FreeRTOS**。单次 ~90µs，高采样数时忙等 45ms，Xeros/UI 任务无法运行 | 高采样率下 UI 帧率从 60→20fps |
| D13 | 滤波器标签 | `oscilloscope_app.c:65-70` | **Low=强滤波(75%历史值)、Hi=弱滤波(25%历史值)，语义反转**。用户预期 Low=弱滤波 | 用户困惑 |
| D14 | 编辑模式退出 | `oscilloscope_input.c:136-143` + `oscilloscope_app.c:383-390` | **编辑中 B 长按直接退出 App，未先退出编辑模式**。与框架 slider_item 的"先取消编辑→二次长按退出"行为不一致 | 误触 B 长按 → 丢失当前采样视图 |
| D15 | 编辑键映射 | `oscilloscope_input.c:132-143` | **编辑模式 A 短按=减、B 短按=增，与框架 slider 完全颠倒**（slider 中 A=确认/加、B=取消/减）| 跨 App 肌肉记忆失效 |
| D16 | 正常模式 B 键 | `oscilloscope_input.c:128` | **B 短按=toggle run，替代了全局约定的"上一参数"导航**。7 个参数只能 A 单向循环，回到上一个需绕一圈 | 体验差但设计上有意为之 |
| D17 | 编辑事件残留 | `oscilloscope_input.c:125-137` | **切换编辑模式时未调用 `hal_input_reset_events()`**。可能残留 LONG_PRESS → 进入编辑后立即被消费为退出编辑 | 偶发的"进入后立即退出"现象 |
| D18 | 采样率不真实 | `oscilloscope_app.c:368-369` | **100kHz 实际只有 ~11kHz**。analogRead overhead 约 90µs 但代码标注 20µs，低估 4.5 倍。高采样率选项名不副实 | 时基标签与真实时间轴偏差 ~10 倍 |
| D19 | 触发类型安全 | `oscilloscope_ui.h:35` + `oscilloscope_app.c:162` | **`trigger_level` 为 `int16_t` 可负，但 `scope_draw_trigger_line` 做 `(uint16_t)` 转换**。若未来允许负值 → 巨大正数 → 触发线画到屏幕外 | 潜在类型安全问题 |

### P2 — 改进建议（12 项）

| ID | 模块 | 文件 | 问题 | 建议 |
|----|------|------|------|------|
| D20 | 封装 | `oscilloscope_internal.h:19` | `sample_write_pos` 只在 `oscilloscope_app.c` 使用，不应暴露在 struct 中 | 改为文件级 `static` |
| D21 | 文件长度 | `oscilloscope_app.c` | 392 行逼近 400 上限 | 拆分参数表常量或测试桩到独立文件 |
| D22 | 函数长度 | `oscilloscope_app.c:262` | `scope_update_measurements` 49 行逼近 50 上限 | 提取 `scope_measure_frequency()` |
| D23 | 镜像重复 | `oscilloscope_input.c:24-108` | `param_decrease/increase` 共 84 行对称重复 | 合并为带 direction 参数的单函数 |
| D24 | 状态混合 | `oscilloscope_internal.h:13-23` | `scope_state_t` 混合引擎内部状态与 view 层状态 | 分离为 `scope_engine_state_t` + `oscilloscope_view_state_t` |
| D25 | 约定偏离 | `oscilloscope_app.c:388` | 使用裸 `ui_user_item_try_exit()` 而非封装 `ui_service_user_item_loop()` | 替换为标准封装函数 |
| D26 | 测试覆盖 | `test_oscilloscope.cpp:98` | filter weight 测试太弱（`≤8` 放过非法值 7） | 改为 `EXPECT_EQ(2U)` |
| D27 | 内存浪费 | `oscilloscope_internal.h:12` | `SCOPE_SAMPLE_MAX=2000` 对 80px 屏过量 25 倍 | 减小到 256-512，节省 ~3KB RAM |
| D28 | 布局缓存 | `oscilloscope_ui.c:29-40` | `scope_compute_layout()` 每帧重算 | 缓存到 static，仅在屏幕变化时重新计算 |
| D29 | 分隔线 | `oscilloscope_ui.c:227,278` | 使用 `hal_draw_line` 画水平线 | 改用 `hal_draw_h_line`（M5GFX 硬件加速） |
| D30 | 编辑高亮 | `oscilloscope_ui.c:268-270` | 编辑模式使用青色填充+黑色文字，对比度不足 | 统一为白底黑字（与其他 App 一致） |
| D31 | 选择状态持久 | `oscilloscope_app.c:318-324` | `oscilloscope_init()` 总是 memset 归零 + 硬编码 resume，不支持 session 内状态保持 | 首次 init 时初始化，再次进入时保留 |

---

## 本轮重构排期

| 子阶段 | 目标 | 处理的问题 ID |
|--------|------|---------------|
| 2.4.1 | 引擎正确性修复 | **D1, D2, D3, D6, D7**（波形可见性 + AC 耦合 + 像素死区） |
| 2.4.2 | 测量精度改进 | **D4, D5**（AC 偏移窗口 + 频率滞回） |
| 2.4.3 | 输入交互修复 | **D14, D15, D17**（编辑模式退出 + 按键映射 + 事件残留） |
| 2.4.4 | 渲染性能优化 | **D8, D10, D11**（dirty rect + 轮廓线简化 + 网格密度） |
| 2.4.5 | 颜色与布局 | **D9, D28, D29, D30**（RGB332 颜色 + 缓存 + 分隔线 + 高亮） |

---

## 各问题修复复杂度评估

| ID | 复杂度 | 预计改动 | 风险 |
|----|--------|----------|------|
| D1 | ⭐⭐⭐ | ~30 行 | 中：需修改触发索引到显示坐标的映射，涉及环形缓冲区折回 |
| D2 | ⭐⭐ | ~15 行 | 低：增加重新居中逻辑和 int16_t 缓冲区支持 |
| D3 | ⭐⭐⭐ | ~20 行 | 中：像素映射需处理环缓冲折回；与 D1 关联 |
| D6 | ⭐ | ~2 行 | 极低：修改 clamp 条件 `>=` → `>` |
| D4 | ⭐⭐ | ~10 行 | 低：窗口大小改为与采样率/时基成比例 |
| D5 | ⭐⭐ | ~10 行 | 低：增加滞回带 |
| D14 | ⭐⭐ | ~15 行 | 低：编辑模式分支增加 B 长按处理 |
| D15 | ⭐⭐ | ~5 行 | 低：交换 A/B 短按的增减方向 |
| D17 | ⭐ | ~3 行 | 极低：编辑模式切换前调用 `hal_input_reset_events()` |
| D8 | ⭐⭐ | ~20 行 | 低：增加 dirty flag，仅在变化时重绘 |
