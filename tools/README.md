# Icon Converter — 图标转换工具

将任意图片转换为 Xerintosh UI 框架可用的 XBM 位图 C 头文件。

## 依赖安装

```bash
pip install Pillow
```

## 快速开始

### 1. 转换单张图片

```bash
python tools/icon_converter.py -i my_icon.png
```

输出文件 `my_icon.h`，默认生成 8x8 像素的位图。

### 2. 指定尺寸和名称

```bash
python tools/icon_converter.py -i wifi.png -w 16 -h 16 -n wifi_icon -o icons/wifi_icon.h
```

### 3. 预览效果

```bash
python tools/icon_converter.py -i heart.png --preview
```

终端会显示 ASCII 预览图，方便调整阈值。

### 4. 反转颜色

```bash
python tools/icon_converter.py -i logo.png --invert
```

## 完整参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `-i, --input` | 输入图片路径（必需） | - |
| `-o, --output` | 输出头文件路径 | 与输入同名，`.h` 扩展名 |
| `-n, --name` | 图标变量名前缀 | 从输入文件名推断 |
| `-w, --width` | 目标宽度（像素） | 8 |
| `--height` | 目标高度（像素） | 8 |
| `-t, --threshold` | 二值化阈值（0-255） | 128 |
| `--invert` | 反转黑白 | false |
| `--preview` | 终端 ASCII 预览 | false |

## 在代码中使用

生成的头文件可以直接包含到项目中：

```c
#include "wifi_icon.h"

xerintosh_list_item_t* item = xerintosh_new_list_item("WiFi", custom_icon);
xerintosh_set_item_bitmap(item, icon_wifi_icon_bitmap, ICON_WIFI_ICON_WIDTH, ICON_WIFI_ICON_HEIGHT);
```

## 格式说明

- 输出格式为 **XBM**（X BitMap），每像素 1 bit
- 字节内 bit 顺序从 LSB 到 MSB，与 `hal_draw_xbitmap()` 接口完全兼容
- 建议图标尺寸不超过 16x16 像素，以适配小屏幕设备
