#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_font_small_20.py
====================
Generate a TFT_eSPI .vlw format font library header (font_small_20_new.h)
from a TrueType font using PIL.

.vlw format (reverse-engineered from TFT_eSPI Smooth_font.cpp):
  Header (24 bytes, 6 x uint32_t, BIG-ENDIAN):
    [0]  gCount           - number of glyphs
    [4]  version          - 0x0B (11)
    [8]  yAdvance         - font size in points
    [12] mboxY            - deprecated, 0
    [16] ascent           - baseline -> top of "d" (px)
    [20] descent          - baseline -> bottom of "p" (px)
  Per glyph (28 bytes, 7 x int32_t, BIG-ENDIAN):
    [0]  Unicode codepoint
    [4]  height           - bitmap height
    [8]  width            - bitmap width
    [12] gxAdvance        - cursor x advance
    [16] gdY              - baseline -> bitmap top (signed, + = up)
    [20] gdX              - cursor -> bitmap left (signed, - = left)
    [24] padding          - 0
  Bitmaps (start at offset 24 + 28*gCount):
    Each pixel = 1 byte alpha (0x00 bg, 0xFF fg)
    Length per glyph = width * height
  Trailing:
    1 byte font name length (excluding null)
    font name string + null
    1 byte postscript name length
    postscript name string + null/0
    1 byte: 0=aliased, 1=antialiased

Usage:
    python gen_font_small_20.py
Output:
    include/font_small_20_new.h
"""

import os
import struct
from io import BytesIO

from PIL import Image, ImageDraw, ImageFont

# # ===== 解决 Windows 控制台乱码 =====
# if sys.platform == 'win32':
#     sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')


# -------- Config --------
FONT_PATH = r"LXGWWenKaiMono-Regular.ttf"
FONT_SIZE = 20                                # px

#程序所在目录
PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
OUTPUT_H = os.path.join(PROJECT_ROOT, "include", "font_small_20.h")
CHARSET_FILE = os.path.join(PROJECT_ROOT, "charset.txt")  # 新增：字符集文件路径

# 这两个变量可以保留，或者从字体文件自动提取
FONT_NAME = os.path.splitext(os.path.basename(FONT_PATH))[0]  # embedded in .vlw trailer
FONT_PS_NAME = FONT_NAME

VARIABLE_NAME = "font_small_20"



def u32be(v):
    """Pack unsigned 32-bit big-endian."""
    return struct.pack('>I', v & 0xFFFFFFFF)


def i32be(v):
    """Pack signed 32-bit big-endian."""
    return struct.pack('>i', v)


def build_charset():
    """Return list of characters: ASCII 0x20-0x7E + GB2312 Level 1 (3755 chars)."""
    chars = []
    # ASCII printable (space .. ~)
    for c in range(0x20, 0x7F):
        chars.append(chr(c))
    # GB2312 Level 1: 区 16-55, 位 1-94
    seen = set(chars)
    for qu in range(16, 56):            # 区 16..55
        for wei in range(1, 95):        # 位 1..94
            try:
                gb = bytes([0xA0 + qu, 0xA0 + wei])
                ch = gb.decode('gb2312')
            except (UnicodeDecodeError, ValueError):
                continue
            if ch and ch not in seen:
                chars.append(ch)
                seen.add(ch)
    return chars

def build_charset_from_file(txt_file_path, include_ascii=True):
    """
    从指定的文本文件中读取所有字符，去重后返回字符列表。
    自动跳过空白行、空格、换行符，只保留文字字符。
    
    参数:
        txt_file_path: 文本文件的路径 (例如: "charset.txt")
        include_ascii: 是否包含ASCII可打印字符 (0x20-0x7E)
    
    返回:
        包含文件中所有不重复字符的列表
    """
    charset = []
    seen = set()
    
    # 如果包含ASCII，先添加所有可打印字符
    if include_ascii:
        for c in range(0x20, 0x7F):
            charset.append(chr(c))
            seen.add(chr(c))
    
    try:
        with open(txt_file_path, 'r', encoding='utf-8') as f:
            # 读取整个文件内容
            content = f.read()
            
            # 遍历每个字符，只保留非空白字符（空格、换行、制表符等都跳过）
            for ch in content:
                # 跳过所有空白字符（空格、换行、回车、制表符等）
                if ch.isspace():
                    continue
                # 去重
                if ch not in seen:
                    charset.append(ch)
                    seen.add(ch)
                    
    except FileNotFoundError:
        print(f"警告：找不到 '{txt_file_path}'，仅使用 ASCII 字符。")
    
    print(f"字符集总计: {len(charset)} 个字符")
    return charset



def render_glyph(font, ch, ascent, descent):
    """Render one glyph, return dict with bitmap bytes + metrics.
    Returns None on failure."""
    line_h = ascent + descent
    try:
        bbox = font.getbbox(ch)
        x_advance = font.getlength(ch)
    except Exception:
        return None

    if not bbox or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        # No visible pixels (space, etc.) - still register metrics for x advance
        return {
            'code': ord(ch),
            'width': 0,
            'height': 0,
            'gxAdvance': max(0, int(x_advance)),
            'gdX': 0,
            'gdY': 0,
            'bitmap': b'',
        }

    x0, y0, x1, y1 = bbox
    w = x1 - x0
    h = y1 - y0

    # Render onto full-line-height canvas to ensure correct alpha
    canvas_w = max(int(x_advance), x1, 1)
    img = Image.new('L', (canvas_w, line_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), ch, font=font, fill=255)
    bitmap_img = img.crop(bbox)
    bitmap_bytes = bitmap_img.tobytes()

    # PIL coords: y down, baseline at y = ascent
    # gdY (baseline -> bitmap top, + = up) = ascent - y0
    gdY = ascent - y0
    gdX = x0

    return {
        'code': ord(ch),
        'width': w,
        'height': h,
        'gxAdvance': max(int(x_advance), w),
        'gdX': int(gdX),
        'gdY': int(gdY),
        'bitmap': bitmap_bytes,
    }


def main():
    print(f"[1/4] Loading font: {FONT_PATH} @ {FONT_SIZE}px")
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    ascent, descent = font.getmetrics()
    print(f"      ascent={ascent}  descent={descent}  line_h={ascent + descent}")

    # 这里包含所有1级汉字，生成的字体文件太大了。
    # print("[2/4] Building character set (ASCII + GB2312 Level 1)...")
    # chars = build_charset()
    # print(f"      Total characters: {len(chars)}")

    print("[2/4] Building character set from file...")
    chars = build_charset_from_file(CHARSET_FILE)
    print(f"      Total characters: {len(chars)}")

    print("[3/4] Rendering glyphs...")
    glyphs = []
    skipped = 0
    for ch in chars:
        g = render_glyph(font, ch, ascent, descent)
        if g is None:
            skipped += 1
            continue
        glyphs.append(g)
    print(f"      Rendered: {len(glyphs)} glyphs  (skipped: {skipped})")

    # ---- Build .vlw binary ----
    print("[4/4] Packing .vlw and writing C header...")
    buf = BytesIO()

    # Header (24 bytes)
    buf.write(u32be(len(glyphs)))         # gCount
    buf.write(u32be(0x0B))                # version
    buf.write(u32be(FONT_SIZE))           # yAdvance (font size in points)
    buf.write(u32be(0))                   # mboxY (deprecated)
    buf.write(u32be(ascent))              # ascent
    buf.write(u32be(descent))             # descent

    # Per-glyph metadata (28 bytes each)
    # Bitmap data starts at: 24 + len(glyphs)*28
    for g in glyphs:
        buf.write(i32be(g['code']))
        buf.write(i32be(g['height']))
        buf.write(i32be(g['width']))
        buf.write(i32be(g['gxAdvance']))
        buf.write(i32be(g['gdY']))
        buf.write(i32be(g['gdX']))
        buf.write(i32be(0))                # padding

    # Bitmaps
    total_bitmap_bytes = 0
    for g in glyphs:
        buf.write(g['bitmap'])
        total_bitmap_bytes += len(g['bitmap'])

    # Trailer: font name + postscript name + antialias flag
    name_bytes = FONT_NAME.encode('ascii')
    buf.write(struct.pack('B', len(name_bytes)))
    buf.write(name_bytes)
    buf.write(b'\x00')

    ps_bytes = FONT_PS_NAME.encode('ascii')
    buf.write(struct.pack('B', len(ps_bytes)))
    buf.write(ps_bytes)
    buf.write(b'\x00')                    # null terminator (also serves as non-AA flag bit)
    buf.write(b'\x01')                    # 1 = antialiased

    vlw_data = buf.getvalue()

    # ---- Write .h file ----
    os.makedirs(os.path.dirname(OUTPUT_H), exist_ok=True)
    n_ascii = sum(1 for g in glyphs if g['code'] <= 0x7F)
    n_cjk = len(glyphs) - n_ascii

    with open(OUTPUT_H, 'w', encoding='utf-8', newline='\n') as f:
        f.write("// ============================================================\n")
        f.write(f"// {os.path.basename(OUTPUT_H)}\n")
        f.write(f"// {FONT_SIZE}px font - generated by gen_font_small_20.py\n")
        f.write("// ============================================================\n")
        f.write(f"// Source font : {FONT_PATH}\n")
        f.write(f"// Font size   : {FONT_SIZE}px\n")
        f.write(f"// Charset     : ASCII ({n_ascii}) + GB2312 Level 1 ({n_cjk})\n")
        f.write(f"// Total glyphs: {len(glyphs)}\n")
        f.write(f"// VLW size    : {len(vlw_data)} bytes ({len(vlw_data)/1024:.1f} KB)\n")
        f.write("// Format      : TFT_eSPI .vlw (big-endian, anti-aliased alpha bitmaps)\n")
        f.write("// ============================================================\n\n")
        f.write(f"#ifndef {VARIABLE_NAME.upper()}_H\n")
        f.write(f"#define {VARIABLE_NAME.upper()}_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"const uint8_t {VARIABLE_NAME}[] PROGMEM = {{\n")

        # 16 bytes per line, hex with trailing comma + space + newline
        for i in range(0, len(vlw_data), 16):
            chunk = vlw_data[i:i+16]
            f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ", \n")

        f.write("};\n\n")
        f.write(f"#endif // {VARIABLE_NAME.upper()}_H\n")

    # ---- Summary ----
    out_size = os.path.getsize(OUTPUT_H)
    print()
    print("=" * 60)
    print("DONE")
    print("=" * 60)
    print(f"Output file     : {OUTPUT_H}")
    print(f"C header size   : {out_size:,} bytes ({out_size/1024/1024:.2f} MB)")
    print(f"VLW binary size : {len(vlw_data):,} bytes ({len(vlw_data)/1024:.1f} KB)")
    print(f"  Header        : 24 bytes")
    print(f"  Glyph meta    : {len(glyphs)*28:,} bytes ({len(glyphs)} x 28)")
    print(f"  Bitmaps       : {total_bitmap_bytes:,} bytes ({total_bitmap_bytes/1024:.1f} KB)")
    print(f"  Trailer       : {len(vlw_data) - 24 - len(glyphs)*28 - total_bitmap_bytes} bytes")
    print(f"Glyphs          : {len(glyphs)} (ASCII: {n_ascii}, GB2312 L1: {n_cjk})")
    print(f"Bytes per glyph : {len(vlw_data)/max(1,len(glyphs)):.1f}")


if __name__ == '__main__':
    main()
