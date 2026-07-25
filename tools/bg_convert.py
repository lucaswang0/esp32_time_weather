#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
背景图转换器 - 调整尺寸、透明化、格式转换
Background Image Converter - Resize, Transparency, Format Conversion

支持真透明 (RGBA) 输出，专为 ESP32 TFT 屏幕背景图优化
Supports true alpha output, optimized for ESP32 TFT screen backgrounds

合并自: bg_png_to_data.py + bg_to_transparent_png.py
参考:   svg_to_png.py 的 RGBA 管道 + 阈值锐化策略
"""

import os
import sys
import argparse
import glob
from PIL import Image


# ============================================================================
# 透明化处理 / Transparency Processing
# ============================================================================

def apply_hard_threshold(img, threshold=230):
    """
    硬边二值化 / Hard threshold binarization
    模仿 svg_to_png.py: a<threshold→0, a>=threshold→255
    边缘略硬但运行时可用单色键 (0x0001) 判定透明
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    r, g, b, a = img.split()
    a = a.point(lambda v: 0 if v < threshold else 255)
    return Image.merge('RGBA', (r, g, b, a))


def apply_soft_threshold(img, threshold=128):
    """
    软阈值剔除 / Soft threshold removal
    a<threshold→0, a>=threshold→保留原值
    保留抗锯齿半透明效果，边缘平滑
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    r, g, b, a = img.split()
    a = a.point(lambda v: 0 if v < threshold else v)
    return Image.merge('RGBA', (r, g, b, a))


def apply_color_replacement(img, bg_color=(255, 255, 255), tolerance=16):
    """
    颜色替换 / Color replacement
    把接近指定颜色的像素设为透明，适合去除纯色背景
    不依赖原图是否带 alpha
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    pixels = img.load()
    width, height = img.size
    bg_r, bg_g, bg_b = bg_color[:3]
    tol = tolerance

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if (abs(r - bg_r) <= tol and
                abs(g - bg_g) <= tol and
                abs(b - bg_b) <= tol):
                pixels[x, y] = (r, g, b, 0)
    return img


# ============================================================================
# 尺寸处理 / Resize & Fit
# ============================================================================

def fit_cover(img, target_width, target_height):
    """
    居中裁剪填满 / Cover
    保持长宽比，居中裁剪多余部分，缩放至精确目标尺寸
    """
    src_width, src_height = img.size
    src_ratio = src_width / src_height
    dst_ratio = target_width / target_height

    if src_ratio > dst_ratio:
        # 原图更宽，按高度裁剪
        new_height = src_height
        new_width = int(src_height * dst_ratio)
        left = (src_width - new_width) // 2
        img = img.crop((left, 0, left + new_width, new_height))
    else:
        # 原图更高，按宽度裁剪
        new_width = src_width
        new_height = int(src_width / dst_ratio)
        top = (src_height - new_height) // 2
        img = img.crop((0, top, new_width, top + new_height))

    return img.resize((target_width, target_height), Image.Resampling.LANCZOS)


def fit_contain(img, target_width, target_height, padding=(0, 0, 0, 0)):
    """
    等比缩放留白 / Contain
    按较小维度等比缩放，剩余区域填 padding 颜色（RGBA）
    """
    src_width, src_height = img.size
    scale = min(target_width / src_width, target_height / src_height)
    new_width = max(1, int(src_width * scale))
    new_height = max(1, int(src_height * scale))

    resized = img.resize((new_width, new_height), Image.Resampling.LANCZOS)

    # 创建目标尺寸的画布，padding 默认全透明
    canvas = Image.new('RGBA', (target_width, target_height), padding)
    offset_x = (target_width - new_width) // 2
    offset_y = (target_height - new_height) // 2

    if resized.mode == 'RGBA':
        canvas.paste(resized, (offset_x, offset_y), resized)
    else:
        canvas.paste(resized.convert('RGBA'), (offset_x, offset_y))

    return canvas


def fit_stretch(img, target_width, target_height):
    """
    直接拉伸 / Stretch
    不保持比例，直接缩放到目标尺寸（可能变形）
    """
    return img.resize((target_width, target_height), Image.Resampling.LANCZOS)


# ============================================================================
# 输出格式 / Output Format
# ============================================================================

def save_png(img, output_path, fmt='rgba', interlace=False):
    """
    按指定格式保存 PNG
    fmt: rgba | palette8 | rgb
    """
    if fmt == 'rgb':
        if img.mode == 'RGBA':
            # 用白色合成透明区域（避免变成黑色）
            bg = Image.new('RGB', img.size, (255, 255, 255))
            bg.paste(img, mask=img.split()[3])
            img = bg
        elif img.mode != 'RGB':
            img = img.convert('RGB')
        img.save(output_path, 'PNG', optimize=True, interlace=interlace)

    elif fmt == 'palette8':
        # 8 位调色板（255 色 + 1 透明色）
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        try:
            quantized = img.quantize(
                colors=255,
                method=Image.MEDIANCUT,
                dither=Image.FLOYDSTEINBERG
            )
        except Exception:
            quantized = img.convert('P', palette=Image.Palette.ADAPTIVE, colors=255)
        if quantized.mode != 'P':
            quantized = quantized.convert('P')
        quantized.save(
            output_path, 'PNG',
            optimize=True, compress_level=9, interlace=interlace
        )

    else:  # rgba
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        img.save(output_path, 'PNG', optimize=True, interlace=interlace)


# ============================================================================
# 主流程 / Main Pipeline
# ============================================================================

def process_image(input_path, output_path, args):
    """处理单张图片，返回 (success, file_size_or_error)"""
    try:
        img = Image.open(input_path)

        # 1. 调整尺寸（始终在 RGBA 管道中处理，保留 alpha）
        if img.mode not in ('RGB', 'RGBA'):
            img = img.convert('RGBA')

        if args.fit == 'cover':
            img = fit_cover(img, args.width, args.height)
        elif args.fit == 'contain':
            img = fit_contain(img, args.width, args.height)
        else:  # stretch
            img = fit_stretch(img, args.width, args.height)

        # 2. 透明化处理（仅当输出格式需要 alpha 时才执行）
        if args.format in ('rgba', 'palette8') and args.mode != 'none':
            if args.mode == 'hard':
                img = apply_hard_threshold(img, args.hard_threshold)
            elif args.mode == 'soft':
                img = apply_soft_threshold(img, args.alpha_threshold)
            elif args.mode == 'color':
                img = apply_color_replacement(img, args.bg_color, args.bg_tolerance)

        # 3. 保存
        save_png(img, output_path, args.format, interlace=False)

        file_size = os.path.getsize(output_path)
        return True, file_size

    except Exception as e:
        return False, str(e)


def parse_color(color_str):
    """解析 #RRGGBB 颜色字符串为 (r, g, b) tuple"""
    s = color_str.lstrip('#')
    if len(s) == 6:
        return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))
    raise ValueError(f'颜色格式无效 / Invalid color format: {color_str}')


def main():
    parser = argparse.ArgumentParser(
        description='背景图转换器 - 调整尺寸、透明化、格式转换\n'
                    'Background Image Converter - Resize, Transparency, Format',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
使用示例 / Examples:
  # 默认：居中裁剪到 320×170，输出 RGBA（真透明）
  python bg_convert.py
  # Default: center-crop to 320x170, output RGBA (true transparency)

  # 等比缩放留白，8 位调色板输出
  python bg_convert.py --fit contain --format palette8
  # Fit-contain, 8-bit palette output

  # 把白色背景变透明
  python bg_convert.py --mode color --bg-color "#FFFFFF" --bg-tolerance 16
  # Replace white background with transparency

  # 硬边二值化，输出 RGB（不透明）
  python bg_convert.py --mode hard --format rgb
  # Hard-threshold binary, opaque RGB output

  # 自定义输入/输出目录与匹配模式
  python bg_convert.py --input-dir ./src --output-dir ./data --pattern "*.png"
  # Custom input/output dirs and file pattern

  # 一键设置屏幕尺寸（覆盖 --width/--height）
  python bg_convert.py --screen-size 480x320
  # One-shot screen size (overrides --width/--height)

下一步 / Next step: 运行 'pio run -t uploadfs' 将背景图上传到设备 SPIFFS
        '''
    )

    # ---- 输入输出 / I/O ----
    parser.add_argument('--input-dir', default='.',
                        help='输入目录 / Input directory (default: current dir)')
    parser.add_argument('--output-dir', default='data',
                        help='输出目录 / Output directory (default: data)')
    parser.add_argument('--pattern', default='bg*.png',
                        help='文件匹配模式 / File pattern (default: bg*.png)')

    # ---- 尺寸 / Size ----
    parser.add_argument('--width', type=int, default=320,
                        help='目标宽度 / Target width (default: 320)')
    parser.add_argument('--height', type=int, default=170,
                        help='目标高度 / Target height (default: 170)')
    parser.add_argument('--screen-size', default=None,
                        help='屏幕尺寸 WxH（如 480x320），会覆盖 --width/--height / '
                             'Screen size WxH (e.g. 480x320), overrides --width/--height')
    parser.add_argument('--fit', choices=['cover', 'contain', 'stretch'],
                        default='cover',
                        help='缩放策略 / Fit mode: '
                             'cover (居中裁剪填满 / center-crop to fill), '
                             'contain (等比留白 / fit with padding), '
                             'stretch (直接拉伸 / stretch to fit)')

    # ---- 透明化 / Transparency ----
    parser.add_argument('--mode', choices=['hard', 'soft', 'color', 'none'],
                        default='none',
                        help='透明化模式 / Transparency mode: '
                             'hard (硬边二值化 / hard threshold), '
                             'soft (软阈值剔除 / soft threshold), '
                             'color (颜色替换 / color replacement), '
                             'none (不处理 / no-op)')
    parser.add_argument('--hard-threshold', type=int, default=230,
                        help='硬边二值化阈值 (0-255) / Hard threshold (default: 230)')
    parser.add_argument('--alpha-threshold', type=int, default=128,
                        help='软阈值剔除阈值 (0-255) / Soft alpha threshold (default: 128)')
    parser.add_argument('--bg-color', default='#FFFFFF',
                        help='颜色替换的目标色 / Background color to replace (default: #FFFFFF)')
    parser.add_argument('--bg-tolerance', type=int, default=16,
                        help='颜色替换容差 (0-255) / Color tolerance (default: 16)')

    # ---- 输出 / Output ----
    parser.add_argument('--format', choices=['rgba', 'palette8', 'rgb'],
                        default='rgba',
                        help='输出格式 / Output format: '
                             'rgba (完整 alpha / full alpha), '
                             'palette8 (8 位调色板 / 8-bit palette), '
                             'rgb (不透明 / opaque)')

    args = parser.parse_args()

    # 解析颜色
    if args.mode == 'color':
        try:
            args.bg_color = parse_color(args.bg_color)
        except ValueError as e:
            parser.error(str(e))

    # 解析屏幕尺寸（覆盖 --width/--height）
    if args.screen_size:
        try:
            parts = args.screen_size.lower().replace('*', 'x').split('x')
            if len(parts) != 2:
                raise ValueError
            w, h = int(parts[0].strip()), int(parts[1].strip())
            if w <= 0 or h <= 0:
                raise ValueError
            args.width, args.height = w, h
        except (ValueError, AttributeError):
            parser.error(
                f'无效的屏幕尺寸格式 / Invalid --screen-size: {args.screen_size}，'
                f'应为 WxH（如 480x320）/ expected WxH (e.g. 480x320)'
            )

    # 查找文件
    search_pattern = os.path.join(args.input_dir, args.pattern)
    png_files = glob.glob(search_pattern)

    if not png_files:
        print(f'⚠️ 未找到匹配文件 / No files found: {search_pattern}')
        print(f'提示 / Tip: 将背景图命名为 bg01.png, bg02.png 等放在 {os.path.abspath(args.input_dir)}/ 目录下')
        return 1

    # 创建输出目录
    os.makedirs(args.output_dir, exist_ok=True)

    # 打印配置
    print('=' * 60)
    print(f'输入目录 / Input:    {os.path.abspath(args.input_dir)}')
    print(f'输出目录 / Output:   {os.path.abspath(args.output_dir)}')
    print(f'目标尺寸 / Size:     {args.width}×{args.height}')
    print(f'缩放策略 / Fit:      {args.fit}')
    print(f'透明模式 / Mode:     {args.mode}', end='')
    if args.mode == 'hard':
        print(f' (threshold={args.hard_threshold})')
    elif args.mode == 'soft':
        print(f' (threshold={args.alpha_threshold})')
    elif args.mode == 'color':
        print(f' (color=#{args.bg_color[0]:02X}{args.bg_color[1]:02X}{args.bg_color[2]:02X}, tol={args.bg_tolerance})')
    else:
        print()
    print(f'输出格式 / Format:   {args.format}')
    print(f'匹配文件 / Files:    {len(png_files)} 个')
    print('=' * 60)

    # 处理
    success = 0
    fail = 0
    total_size = 0

    for png_path in sorted(png_files):
        filename = os.path.basename(png_path)
        output_path = os.path.join(args.output_dir, filename)

        ok, result = process_image(png_path, output_path, args)

        if ok:
            size_kb = result / 1024
            total_size += result
            print(f'✅ {filename} -> {args.width}×{args.height} ({size_kb:.1f} KB)')
            success += 1
        else:
            print(f'❌ {filename}: {result}')
            fail += 1

    print('=' * 60)
    print(f'处理完成 / Done! 成功: {success}, 失败: {fail}')
    print(f'总大小 / Total: {total_size / 1024:.1f} KB')
    print(f'输出目录 / Output: {os.path.abspath(args.output_dir)}')
    print()
    print('下一步 / Next: 运行 \'pio run -t uploadfs\' 将背景图上传到设备 SPIFFS')

    return 0 if fail == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
