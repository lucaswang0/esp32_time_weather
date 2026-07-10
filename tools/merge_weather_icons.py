#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
合并 weather_icons 目录下的所有图标头文件为一个完整的头文件

使用方法:
    python merge_weather_icons.py                    # 默认参数运行
    python merge_weather_icons.py --input ./weather_icons    # 指定输入目录
    python merge_weather_icons.py --output ./include/weather_icons.h  # 指定输出文件
"""

import os
import argparse

def merge_headers(input_dir, output_path):
    """合并所有图标头文件"""
    icon_codes = [
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
    
    width, height = 64, 64
    icon_names = []
    
    # 收集所有图标数组定义
    all_definitions = []
    
    for code in icon_codes:
        header_path = os.path.join(input_dir, f'weather_icon_{code}.h')
        if os.path.exists(header_path):
            with open(header_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # 提取数组定义部分
            lines = content.split('\n')
            in_array = False
            array_lines = []
            for line in lines:
                if 'const uint16_t' in line and 'PROGMEM' in line:
                    in_array = True
                    array_lines.append(line)
                elif in_array:
                    array_lines.append(line)
                    if line.strip() == '};':
                        in_array = False
            
            all_definitions.append('\n'.join(array_lines))
            icon_names.append((code, f'weather_icon_{code}'))
            print(f"✅ 合并图标: {code}")
        else:
            print(f"⚠️ 未找到: weather_icon_{code}.h")
    
    # 获取尺寸信息（从第一个图标）
    if icon_names:
        first_header = os.path.join(input_dir, f'weather_icon_{icon_names[0][0]}.h')
        if os.path.exists(first_header):
            with open(first_header, 'r', encoding='utf-8') as f:
                content = f.read()
            import re
            match = re.search(r'#define\s+WEATHER_ICON_(\d+)_WIDTH\s+(\d+)', content)
            if match:
                width = int(match.group(2))
            match = re.search(r'#define\s+WEATHER_ICON_(\d+)_HEIGHT\s+(\d+)', content)
            if match:
                height = int(match.group(2))
    
    # 生成合并后的头文件
    merged = f"""// ============================================================
// 天气图标集合 - 自动合并生成
// 尺寸: {width}×{height} 像素
// 格式: RGB565 (16-bit)
// ============================================================

#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

#include <pgmspace.h>

#define WEATHER_ICON_WIDTH {width}
#define WEATHER_ICON_HEIGHT {height}

"""
    
    merged += '\n\n'.join(all_definitions) + '\n\n'
    
    merged += """inline const uint16_t* getWeatherIcon(int weatherCode) {
    switch(weatherCode) {\n"""
    
    for code, name in icon_names:
        merged += f'        case {code}: return {name};\n'
    
    merged += """        default: return weather_icon_999;
    }
}

#endif
"""
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(merged)
    
    print(f"\n✅ 合并完成! 输出文件: {output_path}")
    print(f"   图标数量: {len(icon_names)}")
    print(f"   图标尺寸: {width}x{height}")

def main():
    parser = argparse.ArgumentParser(description='合并天气图标头文件')
    parser.add_argument('--input', help='图标头文件输入目录', default='weather_icons')
    parser.add_argument('--output', help='合并后输出文件路径', default='include/weather_icons.h')
    
    args = parser.parse_args()
    
    merge_headers(args.input, args.output)

if __name__ == '__main__':
    main()
