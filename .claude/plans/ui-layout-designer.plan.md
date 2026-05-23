# Plan: UI Layout Designer Tool（UI 布局设计器）

**Source PRD**: 用户自由文本需求
**Complexity**: Medium（中等）

## Summary

为 M5Stick（80×160 屏幕）开发一个纯前端的 Web UI 布局设计器。用户可以在放大的像素级画板上通过鼠标绘制矩形、圆形、直线、文字，放置图标和 UI 控件占位符，最终导出为 JSON 布局文件，供 LLM 阅读并生成对应的嵌入式 C 代码。

工具将以一组静态 Web 文件（HTML/CSS/JS）形式置于 `tools/ui-layout-designer/` 目录下，用户直接用浏览器打开 `index.html` 即可使用，无需构建步骤。

---

## Patterns to Mirror

| Category | Source | Pattern |
|---|---|---|
| Naming | `src/ui/ui_*.h` | UI 相关使用 `ui_` 前缀，工具使用独立命名空间 |
| Screen Size | `src/hal/hal_display.h:22-25` | Native 测试环境固定 80×160，设计器以此作为默认画布尺寸 |
| Draw API | `src/hal/hal_display.h:53-141` | 绘制原语对齐 HAL API：pixel, line, rect, fill_rect, round_rect, circle, string, xbitmap |
| Colors | `src/hal/hal_display.h:32-34` | RGB565 颜色空间，设计器需支持黑白主色 + 强调色 |
| Item Types | `src/ui/ui_item.h:127-134` | 控件类型映射：list/switch/slider/button/user |
| Tool Location | `tools/icon_converter.py` | 新工具同样放在 `tools/` 目录下，附带 README |

---

## Files to Change

| File | Action | Why |
|---|---|---|
| `tools/ui-layout-designer/index.html` | CREATE | 主页面：左侧工具栏、中间画布、右侧属性面板 |
| `tools/ui-layout-designer/style.css` | CREATE | 样式：暗色主题、像素风 UI、紧凑布局 |
| `tools/ui-layout-designer/app.js` | CREATE | 应用主逻辑：状态管理、事件绑定、工具切换 |
| `tools/ui-layout-designer/canvas.js` | CREATE | 画布引擎：网格渲染、缩放、元素绘制、选择高亮 |
| `tools/ui-layout-designer/elements.js` | CREATE | 元素模型：定义所有可绘制元素的数据结构和渲染方法 |
| `tools/ui-layout-designer/exporter.js` | CREATE | 导出/导入：JSON 序列化、反序列化、文件下载 |
| `tools/ui-layout-designer/preview-font.js` | CREATE | 字体预览：用 Canvas 模拟开发板默认字体的像素级渲染 |
| `tools/ui-layout-designer/README.md` | CREATE | 使用说明、快捷键列表、布局文件格式文档 |
| `tools/README.md` | UPDATE | 在现有图标转换器说明后追加新工具入口 |

---

## Tasks

### Task 1: 页面骨架与整体布局
- **Action**: 创建 `index.html` + `style.css`，搭建三栏布局（左工具栏、中画布、右属性面板）
- **Mirror**: 工具放在 `tools/` 下，与 `icon_converter.py` 平级
- **Validate**: 用浏览器打开 `index.html`，确认三栏布局正常显示

### Task 2: 画布引擎与网格系统
- **Action**: 实现 `canvas.js`，包含：
  - 80×160 逻辑画布，显示缩放（默认每逻辑像素占 4×4 屏幕像素 → 画布显示尺寸 320×640）
  - 可选网格线（每 8 像素一条加粗线，方便对齐）
  - 背景色/前景色切换
  - 画布坐标到逻辑坐标的转换
- **Mirror**: 对齐 `hal_display.h` 的坐标系统，原点在左上角
- **Validate**: 画布正确显示网格，鼠标移动时状态栏显示逻辑坐标

### Task 3: 绘制工具（矩形、圆形、直线）
- **Action**: 实现鼠标拖拽绘制：
  - 空心/实心矩形
  - 空心/实心圆角矩形
  - 圆形（以拖拽距离为半径）
  - 直线段
  - 橡皮擦（清除区域内的元素）
- **Mirror**: 绘制参数（x, y, w, h, r, color, fill）对齐 HAL API 参数风格
- **Validate**: 各种形状可以正常绘制并显示在画布上

### Task 4: 文字工具
- **Action**: 实现文字放置：
  - 点击画布弹出输入框（或侧边栏输入）
  - 支持 ASCII 和中文输入
  - 使用等宽像素字体渲染预览（如 Press Start 2P 或自制位图字体）
  - 字体大小可选（与开发板实际字体高度对应：8px / 12px / 16px）
  - 文字颜色可选（白/黑/绿）
- **Mirror**: 对齐 `hal_draw_string` / `hal_draw_utf8` 的文本渲染行为
- **Validate**: 输入的文字正确显示在画布上，位置和换行符合小屏幕预期

### Task 5: 图标与控件占位符
- **Action**: 实现组件面板：
  - **图标**：预设几个常用 XBM 图标占位符（WiFi、蓝牙、电池、心形等），拖拽到画布上显示为 8×8 / 16×16 的位图预览
  - **控件占位符**：list_item、switch_item、slider_item、button_item、user_item，每种显示为带标签的标准尺寸矩形框，标注控件类型
- **Mirror**: 控件尺寸参考 `ui_item.h` 中的常量和实际渲染尺寸
- **Validate**: 拖拽组件到画布上后，元素列表中正确显示

### Task 6: 元素选择与属性编辑
- **Action**: 实现右侧属性面板：
  - 点击画布上的元素选中，显示选择框（虚线边框）
  - 属性面板可编辑：x, y, w, h, r, color, fill, text content, font size, layer
  - 支持键盘方向键微调位置（1 像素）
  - 支持 Delete 键删除
  - 图层上移/下移
- **Mirror**: 属性命名对齐代码中的结构体字段命名
- **Validate**: 修改属性后画布实时更新，元素位置精确到像素

### Task 7: 导出与导入（JSON 布局文件）
- **Action**: 实现 `exporter.js`：
  - 导出为 JSON，结构如下：
    ```json
    {
      "meta": {
        "device": "M5Stick",
        "screen_width": 80,
        "screen_height": 160,
        "version": "1.0"
      },
      "elements": [
        { "id": "rect_1", "type": "rectangle", "x": 10, "y": 20, "w": 60, "h": 30, "fill": false, "color": "#FFFFFF" },
        { "id": "text_1", "type": "text", "x": 15, "y": 35, "content": "Hello", "font_size": 8, "color": "#FFFFFF" },
        { "id": "icon_1", "type": "icon", "x": 2, "y": 2, "icon_name": "wifi", "w": 8, "h": 8 },
        { "id": "ctrl_1", "type": "control", "subtype": "switch_item", "x": 0, "y": 50, "w": 80, "h": 18, "label": "WiFi" }
      ]
    }
    ```
  - 导入 JSON 文件还原画布状态
  - 导出 LLM Prompt：一键生成供大语言模型阅读的"布局描述"文本
- **Mirror**: 颜色使用 `#RRGGBB` 十六进制字符串，与 RGB565 可互转
- **Validate**: 导出文件可用文本编辑器打开并正确解析，导入后画布状态恢复

### Task 8: LLM Prompt 生成器
- **Action**: 在导出菜单中增加"生成 LLM 提示"功能：
  - 将画布上的所有元素转换为结构化的自然语言描述
  - 附带屏幕尺寸和坐标系说明
  - 提示 LLM 根据此布局生成 Xerintosh UI 框架的 C 代码
  - 可直接复制到剪贴板
- **Validate**: 生成的提示文本语义清晰，包含足够信息让 LLM 理解布局意图

### Task 9: 文档与集成
- **Action**: 编写 `README.md`，更新顶层 `tools/README.md`
- **Mirror**: 与 `tools/icon_converter/README.md` 风格一致（安装、快速开始、参数表、示例）
- **Validate**: README 在浏览器中渲染正常，示例可直接复制使用

---

## Validation

```bash
# 1. 打开设计器
open tools/ui-layout-designer/index.html

# 2. 基本绘制验证：画一个矩形 + 一段文字 + 一个控件占位符，然后导出 JSON
cat layout_export.json | python -m json.tool > /dev/null && echo "JSON valid"

# 3. 导入验证：刷新页面后导入刚才的 JSON，确认画布状态一致

# 4. LLM Prompt 验证：检查生成的提示是否包含所有元素及其坐标
```

---

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| 中文像素字体在浏览器中渲染效果与开发板差异大 | HIGH | 使用 Canvas 手动绘制位图字体字形，或选用最接近的等宽像素字体（如 "Zpix" / "WenQuanYi Bitmap Song"），并在 README 中注明"预览仅供参考，实际渲染以开发板为准" |
| 画布缩放后鼠标坐标转换精度问题 | MEDIUM | 所有内部计算使用逻辑坐标（80×160），仅在渲染时乘缩放系数；使用 `Math.round()` 确保最终坐标为整数像素 |
| 工具文件较多，单文件 vs 多文件的取舍 | LOW | 采用多文件模块化结构，便于维护；最终可用简单构建脚本合并为单文件 HTML（可选） |
| 控件占位符尺寸与实际代码渲染不一致 | MEDIUM | 控件尺寸参考 `ui_item.h` 常量和实际代码中的渲染逻辑，尽量保持同步；在元素数据中标注"仅供参考" |

---

## Acceptance Criteria

- [ ] 所有任务完成
- [ ] 用浏览器打开 `index.html` 可直接使用，无需服务器或构建步骤
- [ ] 可以在 80×160 画布上绘制矩形、圆形、直线、文字、图标、控件占位符
- [ ] 元素可以被选中、移动、调整大小、修改属性、删除
- [ ] 可以导出 JSON 布局文件，文件格式符合上述规范
- [ ] 可以导入 JSON 布局文件还原画布状态
- [ ] 可以生成 LLM 可用的布局描述提示文本
- [ ] `README.md` 文档完整，包含使用示例和布局文件格式说明
- [ ] 顶层 `tools/README.md` 已更新，包含新工具入口
