"""帧源抽象与画面适配（旋转、按目标宽高比居中/左/右裁剪后 LANCZOS 缩放）。"""
from __future__ import annotations

from PIL import Image

VALID_ALIGN = ("left", "center", "right")
VALID_ROTATION = (0, 90, 180, 270)

# 顺时针角度 -> PIL 逆时针旋转常量（PIL 的 ROTATE_x 为逆时针）
_ROTATE_CW = {
    90: Image.Transpose.ROTATE_270,
    180: Image.Transpose.ROTATE_180,
    270: Image.Transpose.ROTATE_90,
}


def normalize_rotation(rotation) -> int:
    """把任意输入归一为 0/90/180/270；非法值按 0 处理。"""
    try:
        angle = int(rotation)
    except (TypeError, ValueError):
        return 0
    return angle if angle in VALID_ROTATION else 0


def rotate_cw(image: Image.Image, rotation: int) -> Image.Image:
    """按顺时针角度旋转 0/90/180/270 度；其他角度原样返回。"""
    op = _ROTATE_CW.get(normalize_rotation(rotation))
    return image.transpose(op) if op else image


class FrameSource:
    """帧源基类：生命周期 open→draw_frame→close，直接绘制到目标 canvas。"""

    def __init__(self, resolution: tuple[int, int]):
        self.resolution = resolution

    def open(self) -> None:
        """分配捕获资源（在生成线程内调用）。"""

    def draw_frame(self, canvas: Image.Image) -> bool:
        """把当前帧绘制到 canvas；资源暂不可用（窗口消失等）返回 False。"""
        return True

    def draw_preview(self, canvas: Image.Image) -> bool:
        """预览画面；默认与推流帧一致，需编辑坐标系时由子类覆盖。"""
        return self.draw_frame(canvas)

    def close(self) -> None:
        """释放捕获资源。"""


def rotated_size(size: tuple[int, int], rotation: int) -> tuple[int, int]:
    """旋转 90/270 度后宽高互换。"""
    w, h = size
    return (h, w) if normalize_rotation(rotation) in (90, 270) else (w, h)


def crop_box(src_size: tuple[int, int], target_size: tuple[int, int],
             alignment: str = "center") -> tuple[int, int, int, int]:
    """按目标宽高比计算裁剪框 (x0, y0, w, h)；宽高比一致时为整图。"""
    if alignment not in VALID_ALIGN:
        alignment = "center"
    tw, th = target_size
    sw, sh = src_size
    target_aspect, shot_aspect = tw / th, sw / sh
    if abs(target_aspect - shot_aspect) <= 0.01:
        return (0, 0, sw, sh)
    if shot_aspect > target_aspect:
        # 画面过宽：裁左右
        new_w = int(sh * target_aspect)
        if alignment == "left":
            x0 = 0
        elif alignment == "right":
            x0 = sw - new_w
        else:
            x0 = (sw - new_w) // 2
        return (x0, 0, new_w, sh)
    # 画面过高：垂直居中裁
    new_h = int(sw / target_aspect)
    return (0, (sh - new_h) // 2, sw, new_h)


def fit_crop(image: Image.Image, target_size: tuple[int, int],
             alignment: str = "center", rotation: int = 0) -> Image.Image:
    """先按顺时针角度旋转原图，再按目标宽高比裁剪（3 种水平对齐）并缩放。"""
    rotated = rotate_cw(image, rotation)
    x0, y0, box_w, box_h = crop_box(rotated.size, target_size, alignment)
    return rotated.crop((x0, y0, x0 + box_w, y0 + box_h)).resize(
        target_size, Image.Resampling.LANCZOS)


def grab_to_image(sct, bbox: dict) -> Image.Image:
    """用 mss 抓取指定 bbox 并转成 PIL RGB 图像。"""
    shot = sct.grab(bbox)
    return Image.frombytes("RGB", shot.size, shot.bgra, "raw", "BGRX")
