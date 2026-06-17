# 诊断报告：动画引擎

## 方法
- 静态扫描 + 逐行审查 `src/ui/ui_core.c`、`ui_draw_anim.c`、`ui_anim_row.c`、`ui_types.h`、`ui_context.c/h`
- 审查现有测试覆盖
- 数学分析弹簧动画稳定性
- 日期：2026-06-17

## 优先级定义
- **P0**：潜在崩溃、死循环、内存安全
- **P1**：功能正确但可维护性问题、接口不一致
- **P2**：性能优化、代码规整、测试缺失

## 问题清单

| ID | 模块 | 文件:行号 | 问题描述 | 优先级 | 建议动作 |
|----|------|-----------|----------|--------|----------|
| A1 | anim-core | `ui_core.c:44-58` | `xerintosh_animation()` 只裁剪 speed 上限 (≥99.0f) 但无下限检查；若 speed 异常 >100 则 `100-speed` 为负导致反向移动 | P1 | 增加 lower bound：`if (_speed > 99.0f) _speed = 99.0f; if (_speed < 0.0f) _speed = 0.0f;` |
| A2 | anim-core | `ui_core.c:53-56` | `xerintosh_animation()` 吸附阈值 (1.0f) 和速度裁剪 (99.0f) 为魔数 | P2 | 提取为 `#define ANIM_SNAP_THRESHOLD 1.0f` 和 `#define ANIM_SPEED_MAX 99.0f` |
| A3 | anim-core | `ui_core.c:44` | `xerintosh_animation()` 无返回值；调用方无法判断动画是否已稳定，导致 `ui_anim_row.c` 自己写 settled 判断逻辑 | P1 | 返回 `bool`（true=已稳定），统一 settled 判定 |
| A4 | anim-row | `ui_anim_row.c:48-49` | `xerintosh_anim_row_list_update()` 对每行动画 y 和 w 两个维度，但只检查 y 的 settled 状态；w 仍在动画中时可能误报"已稳定" | P1 | 同时检查 y 和 w 维度 |
| A5 | anim-row | `ui_anim_row.c:59` | `scroll_offset` 被动画驱动但从未纳入 settled 判定 | P2 | 将 scroll_offset 纳入 settled 检查 |
| A6 | anim-exit | `ui_draw_anim.c:100-103` | 退场动画状态机 status 1→2 转换是瞬时的（同一帧），不等待遮罩实际到达目标；极端情况下（超低动画速度）可能导致视觉跳变 | P2 | 增加到达判定：status 1→2 用时检查 `temp_h >= target - 1.0f` |
| A7 | anim-exit | `ui_draw_anim.c:133-138` | 屏幕方向/尺寸切换检测使用 `int16_t` 比较 `SCREEN_HEIGHT`，但 `exit_anim_prev_screen_h` 也是 `int16_t`，而 `SCREEN_HEIGHT` 宏可能隐式转为 `int`，语义一致但类型不清晰 | P2 | 统一为 `int16_t` 显式转换 |
| A8 | anim-speed | `ui_types.h:23-31` | 9 个 `ANIM_SPEED_*` 宏每次使用都调用 `xerintosh_get_context()->anim_speed`（函数调用），高频使用时可缓存 | P2 | 在主循环入口计算一次 `g_anim_speed` 到局部变量，各宏改为传参 |
| A9 | anim-test | `test_native/` | `xerintosh_animation()` 缺少独立单元测试（仅有 `AnimationTest.EasingConverges` 一个测试） | P1 | 新增 `test_animation.cpp`：收敛、吸附、speed 边界、禁用模式 |
| A10 | anim-spring | `ui_core.c:79-101` | 弹簧动画在极端参数 (stiffness=0.40, damping=0.04) 下，Euler 积分特征值 |λ| 逼近 1.0，理论上可产生永不收敛的等幅振荡；虽然吸附条件会在 0.5px 内停止，但振荡幅度可能超过 0.5px 导致永不吸附 | P1 | 增加帧数上限保底 / 或添加非线性阻尼项确保绝对收敛 |

## 架构观察

### 动画系统数据流
```
settings (level 1-10) → settings_anim_speed_value() → g_anim_speed (45-90)
                                                              ↓
ui_types.h  ANIM_SPEED_* 宏 (g_anim_speed ± offset)  → xerintosh_animation()
                                                              ↓
                                              pos += (target - pos) / (100 - speed)
```

### 双动画模式
1. **一阶缓动** (`xerintosh_animation`)：用于列表项、相机、控件、退场遮罩
2. **二阶弹簧** (`xerintosh_spring_animation`)：用于选择器（可选，由 `g_spring_anim_mode` 控制）

### 关键发现
- 两个动画函数职责相似但接口不一致（一个无返回值，一个无返回值但内部调用 invalidate）
- `ANIM_SPEED_*` 宏的偏移量 (±8, ±2, ±4 等) 缺乏注释说明其设计意图
- 无统一的"动画是否进行中"查询机制，各模块自行判断

## 本轮重构排期

| 阶段 | 模块 | 处理的问题 ID |
|------|------|---------------|
| 2.3.1 | 核心动画函数 | A1, A2, A3, A8, A9 |
| 2.3.2 | 行列表动画 | A4, A5 |
| 2.3.3 | 退场动画状态机 | A6, A7 |
| 2.3.4 | 弹簧动画稳定性 | A10 |
| 2.3.5 | 文档同步 | — |
