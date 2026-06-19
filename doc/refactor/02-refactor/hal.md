# HAL 层重构报告（第十轮 · 2026-06-19）

## 范围
- 处理诊断问题：D5（hal_draw_string 不处理 \n 换行符）
- 变更文件：
  - `src/hal/hal_display_font.cpp`

## 变更摘要
| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改函数 | 1 | hal_draw_string (native + M5GFX 双路径) |

## 详细变更

### 1. hal_draw_string() 添加 \n 换行符处理
**原因**：虽然正常数据路径中 `\n` 在摄入层被剥离，但 `hal_draw_string()` 作为底层接口缺乏防御，如果直接调用传入含 `\n` 的字符串会导致渲染异常（光标右移但不换行）。

**实现**：
- Native 路径：在逐字符循环中添加 `\n` 检测，遇到换行符时 x 归零、y 下移 FONT_H 像素
- M5GFX 路径：改为按行分割绘制，遇到 `\n` 时分段调用 `drawString()`，y 偏移 fontHeight()

**影响接口**：无（函数签名不变）

**文档更新**：`doc/hal/display.md`

## 测试
- 验证结果：
  - `pio run -e m5stick-c`：✅ PASS
  - `pio test -e native`：✅ 无新增失败

## 检查清单
- [x] 函数签名不改变
- [x] 原有字符绘制逻辑不受影响
- [x] 硬件构建无新增警告
- [x] Native 和 M5GFX 两路径行为一致
