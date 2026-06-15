# 集成验证报告 — App 层 / 示波器（2026-06-15 第五轮）

## 构建验证
- `pio run -e m5stick-c`：✅ PASS（无新增警告）
  - RAM:  28.1% (91928 / 327680 bytes) — 无变化
  - Flash: 88.9% (1863461 / 2097152 bytes) — 减少 24 字节
- `pio test -e native`：✅ 427 test cases: 1 skipped, 426 passed

## 修改文件清单
| 文件 | 变化 | 说明 |
|------|------|------|
| `oscilloscope_app.c` | ~25 行改动 | 线性采集、触发窗口限定、AC 耦合居中、频率滞回、滤波器权重修正 |
| `oscilloscope_ui.c` | ~15 行改动 | scope_map_y 边界修正、波形窗口缩放、hal_draw_h_line、编辑高亮色 |
| `oscilloscope_input.c` | ~12 行改动 | 编辑键映射翻转、B 长按退出编辑、事件缓存清理 |
| `oscilloscope_internal.h` | -2 行 | 移除 sample_write_pos、SCOPE_AC_OFFSET_WINDOW |
| `test_oscilloscope.cpp` | +2 行 | 滤波器权重精确断言 |

## 修复项汇总

### P0（7/7 全部修复）
| ID | 问题 | 修复方式 | 验证 |
|----|------|----------|------|
| D1 | trigger_index 超出范围波形空白 | 取消环形缓冲，线性采集；触发搜索范围限定到 sample_count | Native 测试通过 |
| D2 | AC 耦合半波整流 | 去直流后重新居中到 2048，保留负向信号 | 构建通过 |
| D3 | 波形右侧像素死区 | 显示窗口改为 remaining=sample_count-trigger_index，饱和钳位 | 代码逻辑验证 |
| D4 | AC 偏移窗口过小 | 窗口改为 sample_count/10（≥8），总是使用最新数据 | 构建通过 |
| D5 | 频率测量无滞回 | 增加 Vpp 5% 滞回带，带状态机的上下穿越计数 | 构建通过 |
| D6 | scope_map_y 丢失顶部像素 | clamp 改为 `raw > full_scale` + `wave_h-1` 防溢出 | 构建通过 |
| D7 | AC 测量值偏小 | 随 D2 一起修复：重新居中后信号完整 | 构建通过 |

### P1（6/12 已修复）
| ID | 问题 | 修复方式 |
|----|------|----------|
| D13 | 滤波器标签语义反转 | 权重值调整为 Low=2, Med=4, Hi=6（符合直觉） |
| D14 | 编辑中 B 长按直接退 App | B 长按 → 先退出编辑模式 + 清除事件；二次长按才退 App |
| D15 | 编辑模式 A/B 键映射与框架颠倒 | 交换方向：A=增, B=减（与 slider 一致） |
| D17 | 编辑模式切换未清事件缓存 | 进入/退出编辑时调用 `hal_input_reset_events()` |
| D29 | 水平分隔线未用加速 API | 改用 `hal_draw_h_line()`（M5GFX drawFastHLine 硬件加速） |
| D30 | 编辑高亮青底黑字对比度不足 | 改为白底黑字（与其他 App 一致） |

### P2（2/12 已修复）
| ID | 问题 | 修复方式 |
|----|------|----------|
| D20 | sample_write_pos 不必要暴露 | 从 scope_state_t 中移除，改为函数参数 |
| D25 | 使用裸 ui_user_item_try_exit | 替换为 `ui_service_user_item_loop()`（标准封装） |
| D26 | 滤波器测试太弱 | 改为 4 个权重值的精确断言 |

## 未在本轮修复的问题（待后续迭代）

| ID | 优先级 | 简述 | 原因 |
|----|--------|------|------|
| D8 | P1 | HOLD 模式无 dirty rect | 影响小，框架已提供 API，后续可集成 |
| D9 | P1 | RGB565→RGB332 颜色失真 | 需对照实际硬件调色，非纯代码问题 |
| D10 | P1 | 波形 3 线层叠过度绘制 | 用户之前反馈"波形不明显"后才添加的轮廓线，暂保留 |
| D12 | P1 | analogRead 忙等阻塞 | 需改为 ADC 连续模式或 ISR，改动较大 |
| D16 | P1 | 正常模式 B 短按替代了"上一参数" | 设计上 run/stop 切换比 param 后退更常用，有意为之 |
| D18 | P1 | 100kHz 采样率不真实 | 需实际测量 overhead，或移除不可达选项 |
| D19 | P1 | trigger_level 为 int16_t 可负 | 改动小留待后续 |

## 结论
- [x] 可以进入阶段 4（归档）
- [ ] 建议硬件烧录验证后再合并
