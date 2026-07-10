#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
背景图转换器 - 读取bg*.png转换为8位透明PNG
适用于ESP32显示，减小文件大小
"""

import os
import glob
from PIL import Image

def convert_to_8bit_transparent_png(input_path, output_path):
    """
    转换为8位透明PNG（索引色+Alpha通道）
    使用Pillow的量化功能，无需额外依赖
    """
    print(f"  处理: {os.path.basename(input_path)}")
    
    # 1. 打开图片
    img = Image.open(input_path)
    print(f"    原始模式: {img.mode}, 尺寸: {img.size}")
    
    # 2. 转换为RGBA（确保有透明通道）
    if img.mode in ('P', 'L'):
        img = img.convert('RGBA')
    elif img.mode == 'RGB':
        img = img.convert('RGBA')
    elif img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # 3. 移除白色背景（将纯白变为透明）
    width, height = img.size
    pixels = img.load()
    
    white_count = 0
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            # 如果像素接近白色且不透明，设为透明
            if r > 240 and g > 240 and b > 240 and a > 128:
                pixels[x, y] = (255, 255, 255, 0)
                white_count += 1
    
    print(f"    移除白色像素: {white_count}")
    
    # 4. 量化到256色（包含透明色）
    try:
        # 使用Pillow的quantize
        quantized = img.quantize(
            colors=255,
            method=Image.MEDIANCUT,
            kmeans=0,
            dither=Image.FLOYDSTEINBERG
        )
        
        if quantized.mode != 'P':
            quantized = quantized.convert('P')
            
        print(f"    量化后模式: {quantized.mode}")
        palette = quantized.getpalette()
        if palette:
            print(f"    调色板大小: {len(palette) // 3} 色")
        
        # 保存为8位PNG
        quantized.save(
            output_path,
            "PNG",
            optimize=True,
            compress_level=9
        )
        
    except Exception as e:
        print(f"    量化失败: {e}")
        print("    使用备用方法...")
        
        try:
            # 备用方法
            quantized = img.convert('P', palette=Image.Palette.ADAPTIVE, colors=255)
            quantized.save(
                output_path,
                "PNG",
                optimize=True,
                compress_level=9,
                transparency=0
            )
            print(f"    备用方法完成，模式: {quantized.mode}")
        except Exception as e2:
            print(f"    备用方法也失败: {e2}")
            img.save(output_path, "PNG", optimize=True, compress_level=9)
            print("    使用原始RGBA保存")
    
    # 5. 验证保存后的格式
    try:
        verify_img = Image.open(output_path)
        print(f"    最终模式: {verify_img.mode}")
        
        if verify_img.mode == 'P':
            if verify_img.info.get('transparency') is not None:
                print(f"    ✅ 包含透明通道")
            else:
                print(f"    ⚠️ 未检测到透明通道")
            
            palette = verify_img.getpalette()
            if palette:
                colors = len(palette) // 3
                print(f"    调色板颜色数: {colors}")
                
                file_size = os.path.getsize(output_path) / 1024
                print(f"    文件大小: {file_size:.2f} KB")
        elif verify_img.mode == 'RGBA':
            file_size = os.path.getsize(output_path) / 1024
            print(f"    文件大小: {file_size:.2f} KB (RGBA模式)")
    except Exception as e:
        print(f"    验证失败: {e}")
    
    return True

def batch_convert():
    """批量转换所有背景图"""
    current_dir = os.path.dirname(os.path.abspath(__file__))
    output_dir = os.path.join(current_dir, 'data')
    
    # 创建输出目录
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"📁 创建输出目录: {output_dir}")
    
    # 查找所有背景图 (bg*.png)
    png_files = glob.glob(os.path.join(current_dir, 'bg*.png'))
    
    if not png_files:
        print("⚠️ 未找到背景文件 (bg*.png)")
        print("💡 请将背景图片命名为 bg1.png, bg2.png, ... 放在当前目录")
        return
    
    print(f"✅ 找到 {len(png_files)} 个背景文件")
    print("-" * 50)
    
    success = 0
    for png_path in png_files:
        try:
            file_name = os.path.basename(png_path)
            output_path = os.path.join(output_dir, file_name)
            
            convert_to_8bit_transparent_png(png_path, output_path)
            print(f"  ✅ 已保存: data/{file_name}\n")
            success += 1
            
        except Exception as e:
            print(f"  ❌ 转换失败: {e}")
            import traceback
            traceback.print_exc()
            print()
    
    print("-" * 50)
    print(f"🎉 完成! 成功转换 {success} 个文件")
    print(f"📁 文件位置: {output_dir}")
    print("\n📌 请将 data 目录中的PNG文件上传到SPIFFS")
    print("💡 8位PNG文件大小约为原来的1/3")

def convert_single_file():
    """转换单个文件（交互式）"""
    print("📁 请输入要转换的PNG文件路径（或直接拖入文件）:")
    input_path = input().strip().strip('"').strip("'")
    
    if not os.path.exists(input_path):
        print(f"❌ 文件不存在: {input_path}")
        return
    
    # 获取文件名和输出路径
    file_name = os.path.basename(input_path)
    output_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    output_path = os.path.join(output_dir, file_name)
    
    # 转换
    print("-" * 50)
    if convert_to_8bit_transparent_png(input_path, output_path):
        print(f"\n✅ 转换成功!")
        print(f"📁 输出文件: {output_path}")
        file_size = os.path.getsize(output_path) / 1024
        print(f"📊 文件大小: {file_size:.2f} KB")
    else:
        print(f"\n❌ 转换失败")

def main():
    """主函数"""
    print("=" * 60)
    print("  背景图转8位透明PNG转换器")
    print("=" * 60)
    print()
    
    # 检查是否有 bg*.png 文件
    current_dir = os.path.dirname(os.path.abspath(__file__))
    png_files = glob.glob(os.path.join(current_dir, 'bg*.png'))
    
    if png_files:
        print(f"✅ 发现 {len(png_files)} 个背景文件")
        choice = input("是否批量转换? (Y/n): ").strip().lower()
        
        if choice == '' or choice == 'y' or choice == 'yes':
            batch_convert()
        else:
            convert_single_file()
    else:
        print("⚠️ 未找到背景文件 (bg*.png)")
        print()
        choice = input("是否转换单个文件? (Y/n): ").strip().lower()
        
        if choice == '' or choice == 'y' or choice == 'yes':
            convert_single_file()
        else:
            print("退出程序")

if __name__ == '__main__':
    main()