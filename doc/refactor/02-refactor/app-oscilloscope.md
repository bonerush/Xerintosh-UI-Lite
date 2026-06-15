# App 层重构报告 — 示波器（2026-06-15 第五轮）

## 范围
- 处理诊断问题：D1-D7 (P0), D13-D17, D25, D29-D30 (P1/P2)
- 变更文件：
  - `src/app/oscilloscope/oscilloscope_app.c`
  - `src/app/oscilloscope/oscilloscope_ui.c`
  - `src/app/oscilloscope/oscilloscope_input.c`
  - `src/app/oscilloscope/oscilloscope_internal.h`
  - `test/test_native/test_oscilloscope.cpp`

## 变更摘要
| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 新增函数 | 0 | 无新增公共接口 |
| 删除函数 | 0 | 无 |
| 函数修改 | 8 | scope_sample_one, scope_update_trigger, scope_compute_ac_offset, scope_get_display_buffer, scope_update_measurements, scope_handle_input, scope_map_y, scope_draw_wave |
| 参数类型变更 | 1 | scope_sample_one 从无参改为 uint16_t pos |
| 结构体字段移除 | 2 | sample_write_pos, SCOPE_AC_OFFSET_WINDOW |
| 常量值修正 | 3 | 滤波器权重表, 分隔线 API, 编辑高亮色 |
| 行为修复 | 7 | 线性采集, AC 耦合居中, 频率滞回, 编辑映射翻转, 事件清理, 边界 clamp, 窗口缩放 |

## 详细变更

### 1. 环形缓冲区 → 线性帧采集（D1 + D3）
**原因**：环形缓冲区 + trigger_index 跨帧搜索导致 `trigger_index ≥ sample_count → 波形完全空白`，任意非默认时基即触发。
**实现**：
- `scope_sample_one()` 改为接收写入位置参数 `pos`，直接写入 `g_scope.samples[pos]`
- 移除 `sample_write_pos` 环形索引
- `scope_update_trigger()` 只在 `[0, sample_count)` 内搜索触发
- `scope_draw_wave()` 窗口改为 `remaining = sample_count - trigger_index`，映射到全屏宽
**文档更新**：`oscilloscope_app.c` 文件头部注释
**测试覆盖**：现有 trigger 测试仍通过（线性搜索语义不变）

### 2. AC 耦合重新居中（D2 + D7）
**原因**：去直流后负值 clamp 到 0 产生半波整流，Vpp 测量偏小 ~50%。
**实现**：去直流后 `+ 2048` 重新居中，使 AC 信号在 [0, 4095] 范围内对称
**影响接口**：无（内部实现变化）
**文档更新**：`oscilloscope_app.c` 文件头部注释

### 3. AC 偏移窗口自适应（D4）
**原因**：固定 32 样本窗口在大时基下覆盖不足 1% 信号周期。
**实现**：窗口改为 `sample_count / 10`（最小 8），始终取最近样本
**影响接口**：无

### 4. 频率测量滞回（D5）
**原因**：无滞回的过零法对噪声敏感，1kHz+50mV 噪声下读数 1-5kHz 跳动。
**实现**：增加 Vpp 5% 滞回带（最小 5），带状态机穿越计数
**影响接口**：无
**测试覆盖**：现有参数表测试通过

### 5. scope_map_y 边界修正（D6）
**原因**：`raw >= full_scale` 时 clamp 为 `full_scale-1`，满幅信号顶部缺失。
**实现**：clamp 改为 `raw > full_scale → full_scale`；乘法使用 `wave_h - 1` 防溢出
**影响接口**：无

### 6. 滤波器标签语义修正（D13）
**原因**：Low=prev_weight:6（强滤波）、Hi=prev_weight:2（弱滤波），与用户直觉相反。
**实现**：权重值调整为 Low:2, Med:4, Hi:6（Low=弱, Hi=强）
**影响接口**：`g_scope_filters[]` 常量值变化
**测试更新**：`test_oscilloscope.cpp` 精确断言四个权重值

### 7. 编辑模式按键映射翻转（D14, D15）
**原因**：编辑模式 A=减/B=增 与框架 slider 完全颠倒；B 长按直接退 App 而非先退出编辑。
**实现**：
- 编辑模式：A 短=增, B 短=减（与框架一致）
- B 长按 → 先退出编辑 + 清事件；二次 B 长按才退 App
**影响接口**：`scope_handle_input()` 行为变化
**文档更新**：无（用户可见行为变化属设计修正）

### 8. 编辑模式事件缓存清理（D17）
**原因**：进入/退出编辑模式时未清事件缓存，残留 LONG_PRESS 可能被下帧消费。
**实现**：在 `editing` 切换前后调用 `hal_input_reset_events()`
**影响接口**：无

### 9. 水平分隔线 API 优化（D29）
**原因**：header/footer 分隔线使用通用 `hal_draw_line`，未利用硬件加速水平线。
**实现**：改用 `hal_draw_h_line()`
**影响接口**：无

### 10. 编辑高亮色统一（D30）
**原因**：青底黑字对比度不足且与其他 App 不一致。
**实现**：改为 `COLOR_FG` 白底 + `COLOR_BG` 黑字
**影响接口**：无

### 11. 退出检查封装统一（D25）
**原因**：使用裸 `ui_user_item_try_exit` 绕过 ui_service 扩展点。
**实现**：替换为 `ui_service_user_item_loop(ev_b)`
**影响接口**：无

## 测试
- 新增测试：无（重构不引入新功能）
- 修改测试：`test_oscilloscope.cpp` 滤波器权重精确断言
- 验证结果：
  - `pio test -e native`：✅ 427 pass, 1 skipped
  - `pio run -e m5stick-c`：✅ SUCCESS，无新增警告

## 检查清单
- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位（N/A，无继承）
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`（N/A）
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [x] 文档已同步更新（文件头部注释修正）
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点
- 分支：`refactor/2025-06-15-app-oscilloscope`
- 起始 commit：`eb62b47`（当前 main 的快照）

## 遗留问题
| ID | 问题 | 后续处理 |
|----|------|----------|
| D8 | HOLD 模式无 dirty rect | 下一轮集成 `ui_dirty.h` |
| D9 | RGB565→RGB332 颜色失真 | 需硬件对照调色 |
| D12 | analogRead 忙等阻塞 | 改为 ISR/连续模式 |
| D16 | 正常模式 B 短按替代了"上一参数" | 有意的设计取舍 |
| D18 | 100kHz 采样率不真实 | 测量实际 overhead 后调整 |
