#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Icon Converter — 将用户图片转换为 Xerintosh UI 可用的 XBM 位图头文件

用法示例:
    python icon_converter.py -i wifi.png -o wifi_icon.h -n wifi_icon
    python icon_converter.py -i logo.png -w 16 -h 16 --preview

依赖:
    pip install Pillow
"""

import argparse
import os
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("错误: 缺少 Pillow 库。请运行: pip install Pillow")
    sys.exit(1)


# ═══ 常量 ═══

DEFAULT_WIDTH = 8
DEFAULT_HEIGHT = 8
DEFAULT_THRESHOLD = 128


# ═══ 核心转换逻辑 ═══

def image_to_xbm_data(image: Image.Image, threshold: int, invert: bool) -> bytearray:
    """
    将 PIL Image（已转灰度）转换为 XBM 位图字节数组

    XBM 格式:
        - 每像素 1 bit
        - 每行按字节排列，字节内 bit 顺序从 LSB 到 MSB
        - 1 = 前景色(白), 0 = 背景色(黑)
    """
    pixels = list(image.getdata())
    width, height = image.size

    data = bytearray()
    for row in range(height):
        for byte_col in range(0, width, 8):
            byte_val = 0
            for bit in range(8):
                col = byte_col + bit
                if col >= width:
                    break
                pixel = pixels[row * width + col]
                # 二值化: 灰度值 < threshold 视为黑(0)，>= threshold 视为白(1)
                is_white = pixel >= threshold
                if invert:
                    is_white = not is_white
                if is_white:
                    byte_val |= (1 << bit)
            data.append(byte_val)

    return data


def generate_c_header(name: str, width: int, height: int, data: bytearray) -> str:
    """生成 C 头文件内容"""
    guard = f"ICON_{name.upper()}_H"
    array_name = f"icon_{name}_bitmap"

    # 将字节数组格式化为 C 数组字面量，每行 8 个
    hex_bytes = [f"0x{b:02X}" for b in data]
    lines = []
    for i in range(0, len(hex_bytes), 8):
        line = ", ".join(hex_bytes[i:i+8])
        lines.append(f"    {line}")
    array_body = ",\n".join(lines)

    header = f"""/**
 * @file   {name}_icon.h
 * @brief  自定义图标位图 — 由 icon_converter.py 自动生成
 * @note   尺寸: {width}x{height} 像素，XBM 格式
 *
 * 使用方法:
 *   #include "{name}_icon.h"
 *   item->icon = custom_icon;
 *   item->bitmap_data = {array_name};
 *   item->bitmap_w = {width};
 *   item->bitmap_h = {height};
 */

#ifndef {guard}
#define {guard}

#include <stdint.h>

#define ICON_{name.upper()}_WIDTH  {width}
#define ICON_{name.upper()}_HEIGHT {height}

static const uint8_t {array_name}[] = {{
{array_body}
}};

#endif /* {guard} */
"""
    return header


def ascii_preview(image: Image.Image, threshold: int, invert: bool) -> str:
    """生成终端 ASCII 预览"""
    pixels = list(image.getdata())
    width, height = image.size
    lines = []
    for row in range(height):
        line = ""
        for col in range(width):
            pixel = pixels[row * width + col]
            is_white = pixel >= threshold
            if invert:
                is_white = not is_white
            line += "@@" if is_white else "  "
        lines.append(line)
    return "\n".join(lines)


def sanitize_name(name: str) -> str:
    """将字符串清理为有效的 C 标识符"""
    name = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    name = re.sub(r"_+", "_", name)
    name = name.strip("_")
    if name and name[0].isdigit():
        name = "_" + name
    return name or "icon"


# ═══ CLI ═══

def main():
    parser = argparse.ArgumentParser(
        description="将图片转换为 Xerintosh UI 可用的 XBM 位图 C 头文件",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python icon_converter.py -i wifi.png
  python icon_converter.py -i logo.png -w 16 -h 16 -n logo_icon --preview
  python icon_converter.py -i heart.png -t 100 --invert -o icons/heart.h
        """
    )
    parser.add_argument("-i", "--input", required=True, help="输入图片路径 (png/jpg/bmp/gif 等)")
    parser.add_argument("-o", "--output", default=None, help="输出头文件路径 (默认: 与输入同名，.h 扩展名)")
    parser.add_argument("-n", "--name", default=None, help="图标变量名前缀 (默认从输入文件名推断)")
    parser.add_argument("-w", "--width", type=int, default=DEFAULT_WIDTH, help=f"目标宽度，默认 {DEFAULT_WIDTH}")
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT, help=f"目标高度，默认 {DEFAULT_HEIGHT}")
    parser.add_argument("-t", "--threshold", type=int, default=DEFAULT_THRESHOLD, help=f"二值化阈值 0-255，默认 {DEFAULT_THRESHOLD}")
    parser.add_argument("--invert", action="store_true", help="反转颜色（黑变白，白变黑）")
    parser.add_argument("--preview", action="store_true", help="在终端输出 ASCII 预览")

    args = parser.parse_args()

    # 验证输入文件
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"错误: 输入文件不存在: {args.input}")
        sys.exit(1)

    # 推断名称
    if args.name:
        icon_name = sanitize_name(args.name)
    else:
        icon_name = sanitize_name(input_path.stem)

    # 推断输出路径
    if args.output:
        output_path = Path(args.output)
    else:
        output_path = input_path.with_suffix(".h")

    # 读取并处理图片
    try:
        img = Image.open(args.input).convert("L")
    except Exception as e:
        print(f"错误: 无法读取图片: {e}")
        sys.exit(1)

    # 缩放到目标尺寸（使用 LANCZOS 保持锐利边缘）
    img_resized = img.resize((args.width, args.height), Image.LANCZOS)

    # 转换为 XBM 数据
    xbm_data = image_to_xbm_data(img_resized, args.threshold, args.invert)

    # 生成 C 头文件
    header_content = generate_c_header(icon_name, args.width, args.height, xbm_data)

    # 确保输出目录存在
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 写入文件
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    print(f"生成成功: {output_path}")
    print(f"  变量名: icon_{icon_name}_bitmap")
    print(f"  尺寸:   {args.width}x{args.height}")
    print(f"  数据量: {len(xbm_data)} 字节")

    # 可选预览
    if args.preview:
        preview = ascii_preview(img_resized, args.threshold, args.invert)
        print("\nASCII 预览:")
        print("+" + "--" * args.width + "+")
        for line in preview.split("\n"):
            print("|" + line + "|")
        print("+" + "--" * args.width + "+")

    # 打印使用示例
    print(f"\n使用示例:")
    print(f"  #include \"{output_path.name}\"")
    print(f"  item->icon = custom_icon;")
    print(f"  item->bitmap_data = icon_{icon_name}_bitmap;")
    print(f"  item->bitmap_w = {args.width};")
    print(f"  item->bitmap_h = {args.height};")


if __name__ == "__main__":
    main()
