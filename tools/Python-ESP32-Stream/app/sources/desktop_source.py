"""桌面/显示器捕获帧源：mss 枚举显示器、抓取指定区域、裁剪缩放到画布。"""
from __future__ import annotations

import logging

import mss
from PIL import Image

from .base import FrameSource, fit_crop, grab_to_image

log = logging.getLogger(__name__)


def enum_monitors() -> list[dict]:
    """返回可用显示器列表，每项 {index,label,top,left,width,height}。

    mss.monitors[0] 是所有显示器的虚拟合集，其余为物理显示器。
    """
    with mss.mss() as sct:
        result = []
        for i, mon in enumerate(sct.monitors):
            label = "所有显示器（虚拟）" if i == 0 else f"显示器 {i}"
            result.append({"index": i, "label": label,
                           "top": mon["top"], "left": mon["left"],
                           "width": mon["width"], "height": mon["height"]})
        return result


class DesktopSource(FrameSource):
    """桌面捕获：monitor=显示器序号；region=物理像素矩形或 None（整屏）。"""

    def __init__(self, resolution: tuple[int, int], monitor: int = 0,
                 region: dict | None = None, alignment: str = "center"):
        super().__init__(resolution)
        self.monitor_index = monitor
        self.region = region
        self.alignment = alignment
        self._sct = None

    def open(self) -> None:
        # mss 实例必须在使用它的线程中创建
        self._sct = mss.mss()

    def _resolve_bbox(self) -> dict | None:
        """确定抓取矩形：显式 region 优先，否则取配置显示器整屏。"""
        if self.region:
            return dict(self.region)
        mons = self._sct.monitors
        idx = self.monitor_index
        if idx < 0 or idx >= len(mons):
            log.warning("显示器序号 %s 越界（共 %d 个），回退主显示器",
                        idx, len(mons) - 1)
            idx = 1 if len(mons) > 1 else 0
        return dict(mons[idx])

    def draw_frame(self, canvas: Image.Image) -> bool:
        """抓取桌面并贴入画布；截图异常返回 False 由调用方重试。"""
        if self._sct is None:
            return False
        bbox = self._resolve_bbox()
        try:
            shot = grab_to_image(self._sct, bbox)
        except Exception as e:
            log.warning("桌面抓取失败: %s", e)
            return False
        canvas.paste(fit_crop(shot, canvas.size, self.alignment), (0, 0))
        return True

    def close(self) -> None:
        if self._sct:
            try:
                self._sct.close()
            except Exception:
                pass
            self._sct = None
