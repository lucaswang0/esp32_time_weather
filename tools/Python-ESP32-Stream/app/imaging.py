"""图像处理：伽马/白平衡、脏矩形差分、RGB565 转换、分包打帧（全 numpy 向量化）。

线路协议（与 ESP32 固件 StreamingPlayerPage 严格一致）：
- 包头 12 字节大端：struct '!HHHH I' = x, y, w, h, data_len
- 数据：小端 RGB565，每包 data_len <= max_chunk_data_size
- 心跳包：x=y=0xFFFF, w=h=0, data_len=0
"""
from __future__ import annotations

import struct
from collections.abc import Iterator

import numpy as np
from PIL import Image

HEADER_SIZE = 12
HEARTBEAT_MARKER = 0xFFFF
_HEADER_STRUCT = struct.Struct("!HHHH I")


def apply_correction(img: Image.Image, gamma: float,
                     wb_scale: tuple[float, float, float]) -> np.ndarray:
    """伽马 + 白平衡，返回 HxWx3 uint8 RGB 数组。"""
    rgb = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
    rgb = np.power(rgb, gamma)
    rgb *= np.asarray(wb_scale, dtype=np.float32).reshape(1, 1, 3)
    return np.clip(rgb * 255.0, 0, 255).astype(np.uint8)


def rgb565_bytes(rgb: np.ndarray) -> bytes:
    """HxWx3 uint8 RGB 数组 -> 小端 RGB565 字节流（整张向量化）。"""
    r = rgb[..., 0].astype(np.uint16)
    g = rgb[..., 1].astype(np.uint16)
    b = rgb[..., 2].astype(np.uint16)
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return rgb565.astype("<u2").tobytes()


def image_to_rgb565_bytes(img: Image.Image, gamma: float = 1.0,
                          wb_scale=(1.0, 1.0, 1.0)) -> bytes:
    """PIL 图像 -> 校正后 RGB565 字节流。"""
    rgb = apply_correction(img, gamma, tuple(wb_scale))
    return rgb565_bytes(rgb)


def find_dirty_rect(prev: Image.Image | None, curr: Image.Image,
                    threshold: int) -> tuple[int, int, int, int]:
    """返回包围所有变化像素的外接矩形 (x,y,w,h)；无变化返回 None。

    prev 为空或尺寸不一致时返回整帧矩形。
    """
    if prev is None or prev.size != curr.size:
        return (0, 0, curr.width, curr.height)
    a = np.asarray(prev.convert("RGB"), dtype=np.int16)
    b = np.asarray(curr.convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return (0, 0, curr.width, curr.height)
    changed = np.abs(b - a).sum(axis=2) > threshold
    ys, xs = np.where(changed)
    if ys.size == 0:
        return None
    x0, x1 = int(xs.min()), int(xs.max())
    y0, y1 = int(ys.min()), int(ys.max())
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1)


def iter_chunk_rects(x: int, y: int, w: int, h: int,
                     max_data_size: int) -> Iterator[tuple[int, int, int, int]]:
    """把超过 max_data_size 的矩形按行切成若干不超限的子矩形。"""
    if w * h * 2 <= max_data_size:
        yield (x, y, w, h)
        return
    bytes_per_row = w * 2
    chunk_h = max(1, max_data_size // bytes_per_row)
    for off in range(0, h, chunk_h):
        cur_h = min(chunk_h, h - off)
        yield (x, y + off, w, cur_h)


def pack_packet(x: int, y: int, w: int, h: int, data: bytes) -> bytes:
    """组装 12 字节包头 + RGB565 数据。"""
    return _HEADER_STRUCT.pack(x, y, w, h, len(data)) + data


def heartbeat_packet() -> bytes:
    """心跳保活包（画面静止与队列空闲时周期发送）。"""
    return _HEADER_STRUCT.pack(HEARTBEAT_MARKER, HEARTBEAT_MARKER, 0, 0, 0)


def parse_header(buf: bytes) -> tuple[int, int, int, int, int]:
    """解析包头（供自检/fake receiver 使用）。"""
    return _HEADER_STRUCT.unpack(buf)
