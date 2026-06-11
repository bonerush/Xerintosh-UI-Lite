# UI Layout Designer — UI 布局设计器

一个纯前端的网页工具，用于在像素级精度下设计嵌入式设备（如 M5Stick 80×160）的 UI 界面布局，并导出 JSON 文件供大语言模型阅读生成代码。

## 快速开始

无需安装，直接用浏览器打开即可：

```bash
open tools/ui-layout-designer/index.html
```

或拖拽 `index.html` 到浏览器窗口中。

## 功能概览

| 功能 | 说明 |
|---|---|
| 像素级画板 | 按目标设备分辨率生成画布（默认 80×160），支持 1× ~ 16× 缩放 |
| 网格辅助 | 每 8 像素显示加粗网格线，方便对齐 |
| 绘制工具 | 空心/实心矩形、圆角矩形、圆形、直线段 |
| 文字工具 | 支持 ASCII 和中文输入，字体大小可选（4~24px） |
| 图标库 | 预设 8 种常用图标（WiFi、蓝牙、电池等），拖拽放置 |
| 控件占位符 | list/switch/slider/button/user 五种控件占位符 |
| 属性编辑 | 精确调整坐标、尺寸、颜色、图层 |
| 导出 JSON | 生成机器可读的布局文件 |
| LLM Prompt | 一键生成结构化的自然语言描述，供大语言模型生成 C 代码 |

## 快捷键

| 按键 | 功能 |
|---|---|
| `V` | 选择/移动工具 |
| `R` | 空心矩形 |
| `F` | 实心矩形 |
| `C` | 圆形 |
| `L` | 直线 |
| `T` | 文字 |
| `E` | 橡皮擦 |
| `Delete` | 删除选中元素 |
| `方向键` | 微调选中元素位置（1px）|
| `Shift + 方向键` | 快速微调（5px）|
| `Ctrl + Z` | 撤销 |
| `Ctrl + Y` | 重做 |

## 使用方法

### 1. 选择设备

顶部工具栏左侧选择目标设备，支持 M5Stick、OLED 128×64 等预设，也可自定义分辨率。

### 2. 绘制元素

- 左侧面板选择工具（矩形、圆形、直线等）
- 在画布上按住鼠标拖拽绘制
- 使用颜色选择器切换前景色
- 勾选 "Fill Shape" 绘制实心形状

### 3. 放置文字

- 按 `T` 或点击文字工具
- 在画布上单击，输入文字后确认
- 文字使用等宽字体预览，实际渲染以开发板为准

### 4. 放置图标和控件

- 左侧面板下方的 Icons 和 Controls 区域
- 直接点击图标/控件即可在画布中央放置
- 放置后可用选择工具拖拽调整位置

### 5. 编辑属性

- 点击选中元素，右侧面板显示属性
- 可直接输入精确坐标和尺寸
- 用方向键微调位置
- 调整 "Layer" 改变绘制顺序（数字越大越在上层）

### 6. 导出布局

- **Export JSON**: 下载 `.json` 布局文件
- **Export LLM Prompt**: 生成大语言模型可读的布局描述

## 布局文件格式

```json
{
  "meta": {
    "device": "M5Stick",
    "screen_width": 80,
    "screen_height": 160,
    "version": "1.0",
    "exported_at": "2026-05-22T10:00:00.000Z"
  },
  "elements": [
    {
      "id": "rectangle_1",
      "type": "rectangle",
      "x": 10,
      "y": 20,
      "w": 60,
      "h": 30,
      "color": "#FFFFFF",
      "layer": 0
    },
    {
      "id": "text_1",
      "type": "text",
      "x": 15,
      "y": 35,
      "color": "#FFFFFF",
      "text": "Hello",
      "font_size": 8,
      "layer": 1
    },
    {
      "id": "control_1",
      "type": "control",
      "x": 0,
      "y": 50,
      "w": 80,
      "h": 18,
      "color": "#FFFFFF",
      "subtype": "switch_item",
      "label": "WiFi",
      "layer": 2
    }
  ]
}
```

## 与 Xerintosh 框架的对应关系

| 设计器元素 | HAL API |
|---|---|
| rectangle | `hal_draw_rect()` / `hal_draw_fill_rect()` |
| round-rect | `hal_draw_round_rect()` / `hal_draw_fill_round_rect()` |
| circle | `hal_draw_circle()` |
| line | `hal_draw_line()` |
| text | `hal_draw_string()` / `hal_draw_utf8()` |
| icon | `hal_draw_xbitmap()` |
| control | `xerintosh_new_*_item()` |

## 文件结构

```
tools/ui-layout-designer/
├── index.html    # 主页面
├── style.css     # 样式
├── app.js        # 应用主逻辑
├── canvas.js     # 画布引擎
├── elements.js   # 元素模型与渲染
├── exporter.js   # 导出/导入/LLM Prompt
├── test.html     # 单元测试页面
└── README.md     # 本文档
```

纯静态文件，无需构建工具或服务器。

## 注意事项

- **字体预览仅供参考**：浏览器中的字体渲染与开发板实际字体可能存在差异，文字元素的位置和换行应以实际设备为准。
- **控件尺寸为估算值**：控件占位符的默认尺寸基于 `ui_item.h` 中的常量和经验值，实际渲染可能因内容不同而变化。
- **颜色使用 RGB565**：设计器中使用 `#RRGGBB` 十六进制颜色，导出时会提示对应的 RGB565 值。

## License

MIT — 与主项目一致。
