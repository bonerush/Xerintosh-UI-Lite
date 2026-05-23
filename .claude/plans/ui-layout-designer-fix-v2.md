# Plan: UI 布局工具字体统一与控件所见即所得修复

**Source**: 用户对话（修复 UI Layout Designer 遗留问题）
**Complexity**: Medium

## Summary

修复 UI 布局工具（`tools/ui-layout-designer/`）的两大遗留问题：
1. **字体统一**：设计工具当前使用 `'Courier New', monospace`，与嵌入式设备实际使用的 `efontCN_12`（M5GFX 内置位图字体）不匹配，导致文字宽度、高度、换行全部失真。
2. **控件拖动与所见即所得**：UI 控件（list/switch/slider/button/user）当前只是一个占位符渲染（边框+文字），既无法在画布上自由拖动，外观也与实际设备渲染（像素级图标、精确布局、右侧控件）严重不符。

## 当前状态分析

### 嵌入式设备实际渲染（`src/ui/ui_drawer.c`）
- **字体**: `efontCN_12`，高度 12px，中文全宽 12px，ASCII 半宽 6px
- **列表项布局**:
  - `LIST_ITEM_LEFT_MARGIN = 4`（图标左偏移）
  - `LIST_ITEM_RIGHT_MARGIN = 20`（右侧控件预留）
  - `LIST_ITEM_SPACING = 18`（纵向间距）
  - 图标在 y 中心，文字基线 `_y + hal_get_font_height()/2`
- **右侧控件**:
  - `switch_item`: 外框 11×7 + 内部 3×3 方块（开/关位置不同）
  - `slider_item`: 数值文本（带反色圆角背景框）
  - `list_item/button_item/user_item`: 无右侧控件（或右侧箭头）
- **图标绘制**: 像素级 `hal_draw_h_line/v_line/circle/fill_rect`（`xerintosh_draw_list_icon`）

### 设计工具当前状态
- **字体**: `'Courier New', monospace`，字号 7px/8px（控件）/ 用户可选（文本）
- **控件渲染** (`elements.js:396-444`): 仅绘制虚线边框 + 类型标签 + 右侧粗糙示意
- **拖动**: 代码逻辑存在，但控件放置时 `w=screenW`，边界限制可能阻止移动；且手柄检测优先于移动检测

## Files to Change

| File | Action | Why |
|---|---|---|
| `tools/ui-layout-designer/index.html` | UPDATE | 引入 efont 字体（Google Fonts CDN） |
| `tools/ui-layout-designer/elements.js` | UPDATE | 重写 `measureTextWidth`、重写 `Renderers.control` 为像素级精确渲染、修复控件属性 |
| `tools/ui-layout-designer/canvas.js` | UPDATE | 修复控件拖动逻辑（hitHandle 容差、边界限制） |
| `tools/ui-layout-designer/app.js` | UPDATE | 统一字体大小选择器、控件创建默认尺寸修正 |
| `tools/ui-layout-designer/style.css` | UPDATE | 字体栈统一为 efont |

## Tasks

### Task 1: 字体统一
- **Action**: 在 HTML 中加载 efont 字体；将所有 Canvas `ctx.font` 设置为 `'12px "EFont", monospace'`；重写 `measureTextWidth` 使用 Canvas `measureText` API（字体加载完成后）
- **Mirror**: `src/ui/ui_drawer.c` 中 `hal_get_font_height()` 返回 12，文字基线居中
- **Validate**: 在浏览器控制台验证 `ctx.measureText("中文").width === 24` 和 `ctx.measureText("AB").width === 12`

### Task 2: 控件拖动修复
- **Action**: 修复 `canvas.js` 中 `hitHandle` 容差（从 3px 改为 2px 避免边缘误触）；调整控件创建默认宽度为 `screenW - 8`（留边距）；确保 `moving` 逻辑正确触发
- **Mirror**: `app.js` 中现有的 `dragState.moving` 逻辑
- **Validate**: 在浏览器中测试：选中 control 元素后可在画布范围内自由拖动

### Task 3: 控件像素级精确渲染（所见即所得）
- **Action**: 重写 `Renderers.control`：
  1. 绘制左侧图标（使用 `xerintosh_draw_list_icon` 的 JS 像素级实现，与固件对齐）
  2. 绘制 `content` 文字（efont 字体，基线居中，受可用宽度裁剪）
  3. 绘制右侧控件（switch: 11×7 外框 + 3×3 方块；slider: 数值文本 + 反色框；其他: 右侧箭头）
  4. 绘制选择器高亮框（XOR 反色矩形 + 右侧虚线装饰）——在选中状态下叠加
- **Mirror**: `src/ui/ui_drawer.c` 中 `draw_list_item_xxx` + `xerintosh_draw_list_icon` + `xerintosh_draw_selector`
- **Validate**: 截图对比设计工具渲染与 `test/` 目录下的固件截图（如有）；否则人工检查像素级布局参数

### Task 4: 控件属性面板同步
- **Action**: `app.js` 属性面板中，control 类型应显示 `content`（而非 `label`）字段，且字体大小固定为 12px（移除选择器）
- **Mirror**: 固件中列表项的 `content` 字段
- **Validate**: 选中 control 后属性面板显示 content 文本，可编辑并实时更新渲染

## Validation

```bash
# 1. 本地启动 HTTP 服务器
cd tools/ui-layout-designer && python3 -m http.server 8080

# 2. 用 Playwright 截图验证（手动或自动化）
# 3. 验证清单:
#    - [ ] 中文字符宽度 = 2 × 英文字符宽度
#    - [ ] 控件可从左侧栏拖到画布并自由移动
#    - [ ] switch_item 右侧显示 11×7 像素级开关
#    - [ ] slider_item 右侧显示数值（如 "50"）+ 反色背景框
#    - [ ] list_item 右侧显示箭头
#    - [ ] 所有控件左侧显示与固件一致的像素图标
```

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| efont 字体在浏览器中加载慢或不可用 | Medium | 使用 Google Fonts 的 `Noto Sans Mono CJK SC` 作为 fallback，确保 12px 等宽；或本地提供 woff 文件 |
| Canvas `measureText` 与位图字体度量不匹配 | Medium | 用已知字符集预校准：手动测量 "A"/"中" 的像素宽度，建立校正表 |
| 控件渲染过于复杂导致性能下降 | Low | 控件数量通常 < 20，Canvas 2D 渲染足够快；如有问题可缓存图标位图 |
| 拖动逻辑修改引入回归（其他元素无法拖动） | Medium | 修改后测试 rect/circle/text/icon 的拖动和 resize 仍正常 |

## Acceptance Criteria

- [ ] 所有文字使用与固件一致的 12px 等宽字体，中文全宽/英文半宽
- [ ] UI 控件可在画布上自由拖动、resize
- [ ] 控件渲染与固件像素级对齐（图标、文字位置、右侧控件）
- [ ] 属性面板支持编辑控件的 `content` 文本
- [ ] 其他元素类型（rect/circle/text/icon/line）不受影响
