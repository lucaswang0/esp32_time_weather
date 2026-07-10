#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
背景图 PNG → 调整尺寸 → data 目录
基于 svg_to_png.py 风格，用于处理屏幕背景图
"""

import os
import sys
import argparse
import glob
from PIL import Image


def process_bg_png(input_path, output_path, target_width=320, target_height=170):
    """调整背景图尺寸并保存到 data 目录"""
    try:
        img = Image.open(input_path)

        # 转换为 RGB（不带 alpha 通道，避免 pngdec 库在 RGBA 上的 alpha 混合不确定性导致花屏）
        if img.mode != 'RGB':
            img = img.convert('RGB')

        # 居中裁剪 + 缩放至目标尺寸（保持比例）
        src_width, src_height = img.size
        src_ratio = src_width / src_height
        dst_ratio = target_width / target_height

        if src_ratio > dst_ratio:
            # 原图更宽，按高度裁剪
            new_height = src_height
            new_width = int(src_height * dst_ratio)
            left = (src_width - new_width) // 2
            top = 0
            img = img.crop((left, top, left + new_width, top + new_height))
        else:
            # 原图更高，按宽度裁剪
            new_width = src_width
            new_height = int(src_width / dst_ratio)
            left = 0
            top = (src_height - new_height) // 2
            img = img.crop((left, top, left + new_width, top + new_height))

        # 缩放至精确尺寸
        img = img.resize((target_width, target_height), Image.Resampling.LANCZOS)

        # 保存为 PNG：optimize + compress_level=9 减小文件大小
        # 显式指定 non-interlaced（pngdec 库不支持 interlaced）
        img.save(output_path, 'PNG', optimize=True, interlace=False)

        # 计算文件大小
        file_size = os.path.getsize(output_path)

        return True, file_size
    except Exception as e:
        print(f"❌ {os.path.basename(input_path)}: {e}")
        return False, 0


def main():
    parser = argparse.ArgumentParser(description='背景图 PNG → 调整尺寸 → data 目录')
    parser.add_argument('--input-dir', help='输入目录（默认当前目录）', default='.')
    parser.add_argument('--output-dir', help='输出目录（默认 data）', default='data')
    parser.add_argument('--width', type=int, default=320, help='目标宽度（默认320）')
    parser.add_argument('--height', type=int, default=170, help='目标高度（默认170）')
    parser.add_argument('--pattern', default='bg*.png', help='文件匹配模式（默认 bg*.png）')
    
    args = parser.parse_args()
    
    # 查找输入文件
    search_pattern = os.path.join(args.input_dir, args.pattern)
    png_files = glob.glob(search_pattern)
    
    if not png_files:
        print(f"⚠️ 未找到匹配文件: {search_pattern}")
        print(f"提示: 请将背景图命名为 bg01.png, bg02.png 等放在 {args.input_dir}/ 目录下")
        return
    
    # 创建输出目录
    os.makedirs(args.output_dir, exist_ok=True)
    
    print(f"输入目录: {os.path.abspath(args.input_dir)}")
    print(f"输出目录: {os.path.abspath(args.output_dir)}")
    print(f"目标尺寸: {args.width}×{args.height}")
    print(f"匹配文件: {len(png_files)} 个")
    print("-" * 50)
    
    success = 0
    fail = 0
    total_size = 0
    
    for png_path in sorted(png_files):
        filename = os.path.basename(png_path)
        output_path = os.path.join(args.output_dir, filename)
        
        ok, file_size = process_bg_png(png_path, output_path, args.width, args.height)
        
        if ok:
            size_kb = file_size / 1024
            total_size += file_size
            print(f"✅ {filename} -> {args.width}×{args.height} ({size_kb:.1f} KB)")
            success += 1
        else:
            fail += 1
    
    print("-" * 50)
    print(f"处理完成! 成功: {success}, 失败: {fail}")
    print(f"总大小: {total_size / 1024:.1f} KB")
    print(f"输出目录: {os.path.abspath(args.output_dir)}")
    print()
    print("下一步: 运行 'pio run -t uploadfs' 将背景图上传到设备 SPIFFS")


if __name__ == '__main__':
    main()
