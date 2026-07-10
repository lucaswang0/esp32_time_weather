#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
和风天气SVG图标 → 彩色PNG → TFT_eSPI RGB565 C数组
使用 cairosvg 进行SVG转换，支持复杂路径
"""

import os
import sys
import argparse
from PIL import Image
import xml.etree.ElementTree as ET

os.environ['PATH'] = r'C:\Program Files\GTK3-Runtime Win64\bin;' + os.environ['PATH']
import cairosvg

def svg_to_png(svg_path, output_path, target_size=64):
    try:
        with open(svg_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 强制将图标颜色设为纯白
        content = content.replace('fill="currentColor"', 'fill="#FFFFFF"')
        content = content.replace("fill='currentColor'", "fill='#FFFFFF'")
        
        root = ET.fromstring(content)
        width = int(root.get('width', '16').replace('px', ''))
        height = int(root.get('height', '16').replace('px', ''))
        
        scale = target_size / max(width, height)
        output_width = int(width * scale)
        output_height = int(height * scale)
        
        # 【关键修改 1】删掉 background_color，或者将其设为 None，生成真透明 PNG
        cairosvg.svg2png(
            bytestring=content.encode('utf-8'),
            write_to=output_path,
            output_width=output_width,
            output_height=output_height,
            # background_color='rgb(30, 30, 40)'  # 👈 这行注释掉，不要用任何背景色
        )
        
        # 【关键修改 2】不需要再强制 convert('RGB') 了，保留 RGBA
        # 不要执行 img = img.convert('RGB')，保留透明度
        
        # 处理居中留白
        img = Image.open(output_path)
        if img.size != (target_size, target_size):
            # 创建一张完全透明的背景图
            padded = Image.new('RGBA', (target_size, target_size), (0, 0, 0, 0))
            offset_x = (target_size - img.width) // 2
            offset_y = (target_size - img.height) // 2
            padded.paste(img, (offset_x, offset_y), img) # 使用 img 自身作为遮罩
            padded.save(output_path)
            img = padded
        else:
            img.save(output_path)

        # 阈值锐化：把 cairosvg 抗锯齿产生的半透明边缘像素 (1 <= a < 230) 强制清零，
        # 230 <= a < 255 全部提升到 255（完全不透明）。
        # 阈值选择 230：cairosvg 的AA 边缘通常 a < 150，完全清零；剩余 a=128 附近
        # 的核心像素保留，从而避免"图标变细"。运行时可直接按色键 0x0001 跳过透明
        # 像素，不需要猜测混合色。这样做边缘会略有硬感，但保留图标本身的可视粗度。
        if img.mode == 'RGBA':
            r, g, b, a = img.split()
            a = a.point(lambda v: 0 if v < 230 else 255)
            img = Image.merge('RGBA', (r, g, b, a))
            img.save(output_path)

        return True
    except Exception as e:
        print(f"❌ {os.path.basename(svg_path)}: {e}")
        return False
    

def png_to_rgb565_array(png_path, var_name=None):
    """将PNG图片转换为RGB565格式的C数组"""
    img = Image.open(png_path)

    # 保持 RGBA，透明像素在 RGB565 中用 0x0000（黑色）表示，让 pushImage 配合透明色键
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    width, height = img.size
    pixels = img.load()

    data = []
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            
            # 【核心逻辑】完全根据原生 Alpha 通道来决定透明度
            if a < 128:
                # 透明像素：设为 0x0001 作为透明色
                rgb565 = 0x0001
            else:
                # 不透明像素（白线）正常转换
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data.append(rgb565)


    if var_name is None:
        var_name = os.path.splitext(os.path.basename(png_path))[0]

    return {
        'width': width,
        'height': height,
        'data': data,
        'var_name': var_name
    }

def generate_c_header(result):
    """生成C语言头文件"""
    width = result['width']
    height = result['height']
    data = result['data']
    var_name = result['var_name']
    
    hex_data = ', '.join(f'0x{value:04X}' for value in data)
    
    header = f"""// ============================================================
// 图片: {var_name}
// 尺寸: {width}×{height} 像素
// 格式: RGB565 (16-bit)
// 数组大小: {len(data)} 个元素
// 生成时间: 自动生成
// ============================================================

#ifndef {var_name.upper()}_H
#define {var_name.upper()}_H

#include <pgmspace.h>

#define {var_name.upper()}_WIDTH {width}
#define {var_name.upper()}_HEIGHT {height}

const uint16_t {var_name}[] PROGMEM = {{
    {hex_data}
}};

#endif  // {var_name.upper()}_H
"""
    return header

def main():
    parser = argparse.ArgumentParser(description='SVG图标 → 彩色PNG → RGB565 一键转换')
    parser.add_argument('--svg-dir', help='SVG图标目录', default=None)
    parser.add_argument('--png-dir', help='PNG输出目录', default='weather_png')
    parser.add_argument('--header-dir', help='C头文件输出目录', default='weather_icons')
    parser.add_argument('--size', type=int, default=64, help='图标尺寸（默认64）')
    parser.add_argument('--codes', nargs='+', help='指定要转换的图标代码列表')
    parser.add_argument('--skip-png', action='store_true', help='跳过PNG生成')
    
    args = parser.parse_args()
    
    if args.svg_dir is None:
        base = os.path.dirname(os.path.abspath(__file__))
        candidates = [
            os.path.join(base, 'QWeather-Icons-1.8.0', 'QWeather-Icons-1.8.0', 'icons'),
            os.path.join(base, 'QWeather-Icons-1.8.0', 'icons'),
            os.path.join(base, 'icons'),
        ]
        for path in candidates:
            if os.path.exists(path):
                args.svg_dir = path
                break
        if args.svg_dir is None:
            args.svg_dir = input("请输入SVG图标目录路径: ")
    
    os.makedirs(args.png_dir, exist_ok=True)
    os.makedirs(args.header_dir, exist_ok=True)
    
    icon_codes = args.codes if args.codes else [
        '100', '101', '102', '103', '104',
        '150', '151', '152', '153',
        '300', '301', '302', '303', '304', '305', '306', '307', '308', '309',
        '310', '311', '312', '313', '314', '315', '316', '317', '318',
        '350', '351', '399',
        '400', '401', '402', '403', '404', '405', '406', '407', '408', '409', '410',
        '456', '457', '499',
        '500', '501', '502', '503', '504', '507', '508', '509', '510', '511',
        '512', '513', '514', '515',
        '800', '801', '802', '803', '804', '805', '806', '807',
        '900', '901', '999'
    ]
    
    print(f"SVG目录: {args.svg_dir}")
    print(f"PNG输出: {args.png_dir}")
    print(f"头文件输出: {args.header_dir}")
    print(f"图标尺寸: {args.size}x{args.size}")
    print("-" * 50)
    
    success = 0
    fail = 0
    
    for code in icon_codes:
        svg_path = os.path.join(args.svg_dir, f'{code}.svg')
        if not os.path.exists(svg_path):
            svg_path_fill = os.path.join(args.svg_dir, f'{code}-fill.svg')
            if os.path.exists(svg_path_fill):
                svg_path = svg_path_fill
            else:
                print(f"⚠️ 未找到: {code}.svg")
                fail += 1
                continue
        
        png_path = os.path.join(args.png_dir, f'icon_{code}.png')
        if not args.skip_png:
            if svg_to_png(svg_path, png_path, args.size):
                print(f"✅ {code} -> PNG")
            else:
                fail += 1
                continue
        
        try:
            result = png_to_rgb565_array(png_path, f'weather_icon_{code}')
            header_code = generate_c_header(result)
            header_path = os.path.join(args.header_dir, f'weather_icon_{code}.h')
            with open(header_path, 'w', encoding='utf-8') as f:
                f.write(header_code)
            print(f"✅ {code} -> 头文件 ({result['width']}×{result['height']}, {len(result['data'])} 像素)")
            success += 1
        except Exception as e:
            print(f"❌ {code} 头文件生成失败: {e}")
            fail += 1
    
    print("-" * 50)
    print(f"转换完成! 成功: {success}, 失败: {fail}")
    print(f"头文件目录: {args.header_dir}")

if __name__ == '__main__':
    main()
