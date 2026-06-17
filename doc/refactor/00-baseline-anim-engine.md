# 重构基线报告：动画引擎

## 分支与 Commit
- 分支：`refactor/2026-06-17-anim-engine`
- 起始 commit：`42ef111`

## 构建基线
- `pio run -e m5stick-c`：✅ PASS（RAM 28.2%, Flash 89.1%）
- `pio test -e native`：⚠️ 210/211 通过（1 个 SIGTRAP 为预存内核测试问题，非动画相关）

## 动画相关测试覆盖

| 测试文件 | 测试用例数 | 全部通过 |
|----------|-----------|---------|
| `test_spring_anim.cpp` | 10 | ✅ |
| `test_exit_animation.cpp` | 2 | ✅ |
| `test_anim_row.cpp` | 8 | ✅ |
| `test_native/AnimationTest` | 1 | ✅ |

## 动画引擎文件清单

| 文件 | 行数 | 职责 |
|------|------|------|
| `src/ui/ui_core.c` | 317 | 核心动画函数 + 主循环调度 |
| `src/ui/ui_core.h` | 117 | API 声明 |
| `src/ui/ui_draw_anim.c` | 158 | 退场遮罩动画状态机 |
| `src/ui/ui_anim_row.c` | 91 | 行列表动画工具 |
| `src/ui/ui_anim_row.h` | 91 | 行列表动画 API |
| `src/ui/ui_types.h` | 112 | 速度常量 + 弹簧参数 |
| `src/ui/ui_context.c` | 85 | 全局状态单例 |

## 本次重构范围
- [x] 动画引擎（聚焦）↔ 重点
- [ ] 内核层（跳过）
- [ ] HAL 层（跳过）
- [ ] App 层（仅观察不修改）
- [ ] 文档体系（同步更新）
