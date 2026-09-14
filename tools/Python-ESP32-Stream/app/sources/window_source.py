"""指定窗口捕获帧源：pygetwindow 枚举/查找窗口，mss 抓取窗口区域。"""
from __future__ import annotations

import logging

import mss
import pygetwindow as gw
from PIL import Image

from .base import FrameSource, fit_crop, grab_to_image

log = logging.getLogger(__name__)

# Windows 最小化窗口的固定坐标
_MINIMIZED_POS = -32000


def enum_windows() -> list[dict]:
    """枚举可见的顶层窗口，返回 {title,left,top,width,height,minimized} 列表。"""
    result = []
    for win in gw.getAllWindows():
        title = (win.title or "").strip()
        if not title or win.width <= 0 or win.height <= 0:
            continue
        minimized = win.left <= _MINIMIZED_POS or win.top <= _MINIMIZED_POS
        result.append({
            "title": title, "left": win.left, "top": win.top,
            "width": win.width, "height": win.height, "minimized": minimized,
        })
    result.sort(key=lambda w: w["title"].lower())
    return result


class WindowSource(FrameSource):
    """按窗口标题（子串匹配，取第一个结果）捕获指定窗口。"""

    def __init__(self, resolution: tuple[int, int], window_title: str,
                 alignment: str = "center"):
        super().__init__(resolution)
        self.window_title = window_title or ""
        self.alignment = alignment
        self._sct = None
        self._missing_logged = False

    def open(self) -> None:
        # mss 实例在生成线程内创建；预热 cpu 计数与窗口查找
        self._sct = mss.mss()

    def _find_window_bbox(self) -> dict | None:
        """查找目标窗口并返回 bbox；找不到或最小化返回 None。"""
        if not self.window_title:
            return None
        windows = gw.getWindowsWithTitle(self.window_title)
        if not windows:
            return None
        win = windows[0]
        if win.left <= _MINIMIZED_POS or win.top <= _MINIMIZED_POS:
            return None
        return {"top": win.top, "left": win.left,
                "width": win.width, "height": win.height}

    def draw_frame(self, canvas: Image.Image) -> bool:
        """抓取目标窗口贴入画布；窗口缺失时节流告警并返回 False。"""
        if self._sct is None:
            return False
        bbox = self._find_window_bbox()
        if bbox is None:
            if not self._missing_logged:
                log.warning("未找到窗口或窗口已最小化: %s", self.window_title)
                self._missing_logged = True
            return False
        self._missing_logged = False
        try:
            shot = grab_to_image(self._sct, bbox)
        except Exception as e:
            log.warning("窗口抓取失败: %s", e)
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
