"""帧源抽象与画面适配（按目标宽高比居中/左/右裁剪后 LANCZOS 缩放）。"""
from __future__ import annotations

from PIL import Image

VALID_ALIGN = ("left", "center", "right")


class FrameSource:
    """帧源基类：生命周期 open→draw_frame→close，直接绘制到目标 canvas。"""

    def __init__(self, resolution: tuple[int, int]):
        self.resolution = resolution

    def open(self) -> None:
        """分配捕获资源（在生成线程内调用）。"""

    def draw_frame(self, canvas: Image.Image) -> bool:
        """把当前帧绘制到 canvas；资源暂不可用（窗口消失等）返回 False。"""
        return True

    def close(self) -> None:
        """释放捕获资源。"""


def fit_crop(image: Image.Image, target_size: tuple[int, int],
             alignment: str = "center") -> Image.Image:
    """按目标宽高比裁剪原图（3 种水平对齐），再缩放到 target_size。"""
    if alignment not in VALID_ALIGN:
        alignment = "center"
    tw, th = target_size
    sw, sh = image.size
    target_aspect, shot_aspect = tw / th, sw / sh
    if abs(target_aspect - shot_aspect) <= 0.01:
        return image.resize(target_size, Image.Resampling.LANCZOS)
    if shot_aspect > target_aspect:
        # 画面过宽：裁左右
        new_w = int(sh * target_aspect)
        if alignment == "left":
            box = (0, 0, new_w, sh)
        elif alignment == "right":
            box = (sw - new_w, 0, sw, sh)
        else:
            left = (sw - new_w) // 2
            box = (left, 0, left + new_w, sh)
    else:
        # 画面过高：垂直居中裁
        new_h = int(sw / target_aspect)
        top = (sh - new_h) // 2
        box = (0, top, sw, top + new_h)
    return image.crop(box).resize(target_size, Image.Resampling.LANCZOS)


def grab_to_image(sct, bbox: dict) -> Image.Image:
    """用 mss 抓取指定 bbox 并转成 PIL RGB 图像。"""
    shot = sct.grab(bbox)
    return Image.frombytes("RGB", shot.size, shot.bgra, "raw", "BGRX")
