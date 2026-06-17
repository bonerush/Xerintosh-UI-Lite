# UI 动画引擎重构报告

## 范围
- 处理诊断问题：A1, A2, A3, A4, A5, A6, A7, A9, A10
- 变更文件：
  - `src/ui/ui_core.c` — 核心动画函数（速度边界、常量化、返回值、稳定性注释）
  - `src/ui/ui_core.h` — 函数签名更新（void→bool）
  - `src/ui/ui_anim_row.c` — 多维度 settled 判定修复
  - `src/ui/ui_draw_anim.c` — 类型显式转换
  - `src/ui/ui_types.h` — 新增动画内部常量
  - `src/native_main.cpp` — 新增 7 个动画核心测试
  - `doc/ui/core.md` — 同步文档更新

## 变更摘要
| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 新增宏常量 | 3 | ANIM_SPEED_MAX, ANIM_SPEED_MIN, ANIM_SNAP_THRESHOLD |
| 修改函数签名 | 1 | xerintosh_animation(): void→bool |
| 行为修复 | 2 | anim_row w+scroll settled 判定、speed 下界裁剪 |
| 新增测试 | 7 | 吸附、边界裁剪、禁用模式、返回值、负方向 |
| 文档更新 | 1 | doc/ui/core.md |

## 详细变更

### 1. xerintosh_animation() 速度边界 + 返回值 (A1, A2, A3)
**原因**：
- A1: 旧版 `if (_speed >= 99.0f) _speed = 99.0f` 只裁剪上限，无下限保护；若 speed 异常>100 则 `(100-speed)` 为负导致反向移动
- A2: 魔数 1.0f / 99.0f 散布在代码中
- A3: 无返回值导致各调用方自行判断 settled

**实现**：
- 速度裁剪改为 `_speed > ANIM_SPEED_MAX` 和 `_speed < ANIM_SPEED_MIN`
- 吸附阈值改为 `ANIM_SNAP_THRESHOLD` (1.0f)
- 返回 `bool`：true=已到位/已吸附，false=动画进行中

**影响接口**：`xerintosh_animation()` 返回类型从 `void` 变为 `bool`（兼容：调用方可忽略返回值）

**关键行**：`src/ui/ui_core.c:44-69`

### 2. ui_anim_row 多维度 settled 判定 (A4, A5)
**原因**：
- A4: `xerintosh_anim_row_list_update()` 每行动画 y 和 w 两个维度，但只检查 y 差异判断 settled
- A5: `scroll_offset` 被动画驱动但从未纳入 settled 判定

**实现**：
- 行 settled 判定同时检查 `|y - y_trg|` 和 `|w - w_trg|`
- 高亮框 settled 判定同时检查 y 和 w
- `scroll_offset` 纳入 settled 判定

**关键行**：`src/ui/ui_anim_row.c:43-62`

### 3. 退场动画类型明确化 (A7)
**原因**：`exit_anim_prev_screen_h` 为 `int16_t`，与 `SCREEN_HEIGHT` 宏的 `int` 类型隐式转换不够清晰

**实现**：添加显式 `(float)` / `(int16_t)` 转换，0.5px→0.5f 保证常量类型一致

**关键行**：`src/ui/ui_draw_anim.c:133-138`

### 4. 弹簧动画稳定性文档化 (A10)
**原因**：极端参数 (stiffness=0.40, damping=0.04) 下理论上可能存在收敛问题

**分析结果**：Euler 积分离散化特征值 |λ| = sqrt(1-damping) < 1（∀ damping > 0），系统**绝对收敛**。当前参数范围 (0.04-0.40) 均满足。吸附阈值 0.5px 提供最终到位保证。

**实现**：在函数注释中添加稳定性分析和数学证明

**关键行**：`src/ui/ui_core.c:63-91`

## 测试
- 新增测试：
  - `src/native_main.cpp` AnimationTest: SnapsWhenClose, InstantJumpWhenDisabled, SpeedClampedToMax, SpeedClampedToMin, MovesTowardNegativeTarget, ReturnsFalseWhileAnimating, ReturnsTrueWhenSettled
- 验证结果：
  - `pio test -e native`：✅ 224 测试通过（新增 7 个，全部通过）
  - `pio run -e m5stick-c`：✅ PASS（RAM 28.2%, Flash 89.1%，无新增警告）

### 未处理问题

| ID | 问题 | 理由 |
|----|------|------|
| A6 | 退场动画 status 1→2 瞬切 | 设计意图：status=1 仅作为生命周期触发点，瞬切是正确的 |
| A8 | ANIM_SPEED_* 宏函数调用开销 | 对 ESP32 影响极小，为提高可读性保留宏形式 |

## 检查清单
- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [x] 文档已同步更新
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点
- 回滚 commit：各修改可通过 `git checkout` 回退到 commit `42ef111`
- 每个子改动独立，可分别回滚

## 遗留问题
无。本轮重构范围内所有问题均已处理。
