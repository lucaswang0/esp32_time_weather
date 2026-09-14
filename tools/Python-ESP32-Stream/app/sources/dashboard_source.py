"""仪表盘帧源：按刷新间隔采集 psutil 指标并渲染缓存帧，推流帧直接贴缓存。

布局：组件 x/y 均为 null 时按顺序自动纵向排列；任一坐标填了整数则
绝对定位（x/y 可为单边）。背景支持纯色或本地图片（等比裁剪填充）。
"""
from __future__ import annotations

import logging
import os
import socket
import threading
import time
from collections import deque

import psutil
from PIL import Image, ImageDraw, ImageOps

from ..paths import APP_DIR
from .base import FrameSource
from .temperatures import TemperatureMonitor
from .widgets import MARGIN_X, GAP_Y, build_widget, hexcolor, set_temp_provider

log = logging.getLogger(__name__)

# 全局共享一个温度采集器（后台扫描，引用计数）
_temp_monitor = TemperatureMonitor()


def compute_layout_height(widget_cfgs: list[dict], width: int,
                          gap: int = GAP_Y) -> int:
    """估算自动排列组件的总高（手动定位的组件不计入），供 GUI 超限提示。"""
    total = 2
    for cfg in widget_cfgs:
        if not cfg.get("enabled", True):
            continue
        if cfg.get("x") is not None or cfg.get("y") is not None:
            continue
        w = build_widget(cfg)
        if w:
            total += w.height(width) + gap
    return total


def cover_image(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """等比缩放并居中裁剪到目标尺寸（cover 填充）。"""
    return ImageOps.fit(img.convert("RGB"), size, Image.Resampling.LANCZOS)


class DashboardSource(FrameSource):
    """系统指标仪表盘；采集与推流帧率解耦，画面不变则差分自然为零。"""

    def __init__(self, resolution: tuple[int, int], dash_cfg: dict):
        super().__init__(resolution)
        self._lock = threading.Lock()
        self._configure(dash_cfg)
        self._cache = Image.new("RGB", resolution,
                                hexcolor(self._dash_cfg.get("background")))
        self._rects: list[dict] = []
        self._last_sample = 0.0
        self._prev_net = (0, 0)
        self._prev_net_time = 0.0
        self._hostname = "?"
        self._ip = "?"
        self._boot_time = time.time()
        self._cpu_history: deque = deque(maxlen=64)
        self._bg_cache: Image.Image | None = None
        self._bg_cache_key = None

    def _configure(self, dash_cfg: dict) -> None:
        """解析背景/刷新间隔/启用的组件列表。"""
        self._dash_cfg = dash_cfg
        self._widgets = [build_widget(c) for c in dash_cfg.get("widgets", [])
                         if c.get("enabled", True)]
        self._widgets = [w for w in self._widgets if w is not None]
        self._refresh = max(0.2, float(dash_cfg.get("refresh_interval_sec", 1.0)))
        self._net_adapter = next(
            (w.cfg.get("adapter") for w in self._widgets
             if w.cfg.get("type") == "net" and w.cfg.get("adapter")), None)

    def update_config(self, dash_cfg: dict) -> None:
        """热更新仪表盘配置（组件/颜色/位置/背景），下一次采集生效。"""
        with self._lock:
            self._configure(dash_cfg)
            self._last_sample = 0.0

    def get_rects(self) -> list[dict]:
        """返回最近一帧各组件的画布矩形 [{cfg_index,x,y,w,h,type}]（供拖拽命中）。"""
        with self._lock:
            return list(self._rects)

    @property
    def temp_sensors(self) -> list[dict]:
        return _temp_monitor.scan()

    def kick_temp_scan(self) -> None:
        _temp_monitor.scan_once_async()

    def open(self) -> None:
        psutil.cpu_percent(interval=None)  # 首次调用返回 0，先预热
        set_temp_provider(_temp_monitor)
        _temp_monitor.start()
        self._hostname = socket.gethostname()
        self._ip = self._resolve_ip()
        self._boot_time = psutil.boot_time()
        self._prev_net = self._read_net_counters()
        self._prev_net_time = time.monotonic()

    def _resolve_ip(self) -> str:
        try:
            return socket.gethostbyname(self._hostname)
        except OSError:
            return "?"

    def _read_net_counters(self) -> tuple[int, int]:
        """读取 (发送字节, 接收字节)：指定网卡用 pernic，否则取全局总和。"""
        if self._net_adapter:
            pernic = psutil.net_io_counters(pernic=True)
            io = pernic.get(self._net_adapter)
            if io:
                return io.bytes_sent, io.bytes_recv
        total = psutil.net_io_counters()
        return total.bytes_sent, total.bytes_recv

    def _sample_net_rate(self) -> tuple[float, float]:
        """差分计算 (上行速率, 下行速率)。"""
        now = time.monotonic()
        sent, recv = self._read_net_counters()
        dt = max(0.001, now - self._prev_net_time)
        up = (sent - self._prev_net[0]) / dt
        down = (recv - self._prev_net[1]) / dt
        self._prev_net, self._prev_net_time = (sent, recv), now
        return max(0.0, up), max(0.0, down)

    def _sample_disks(self) -> dict:
        result = {}
        paths = {w.cfg.get("path", "C:\\").upper()
                 for w in self._widgets if w.cfg.get("type") == "disk"}
        for path in paths:
            try:
                u = psutil.disk_usage(path)
                result[path] = {"total": u.total, "used": u.used,
                                "percent": u.percent}
            except OSError:
                continue
        return result

    def _collect_metrics(self) -> dict:
        cpu = psutil.cpu_percent(interval=None)
        self._cpu_history.append(cpu)
        mem = psutil.virtual_memory()
        net_up, net_down = self._sample_net_rate()
        return {
            "now": time.localtime(), "cpu": cpu,
            "cpu_history": self._cpu_history,
            "mem": {"total": mem.total, "used": mem.used,
                    "percent": mem.percent},
            "disks": self._sample_disks(),
            "net_up": net_up, "net_down": net_down,
            "boot_time": self._boot_time,
            "hostname": self._hostname, "ip": self._ip,
        }

    def _resolve_bg_path(self) -> str | None:
        """配置路径支持绝对路径与相对应用目录；不存在返回 None。"""
        path = self._dash_cfg.get("bg_image")
        if not path:
            return None
        if not os.path.isabs(path):
            path = str(APP_DIR / path)
        return path if os.path.isfile(path) else None

    def _background(self) -> Image.Image:
        """构造背景：图片 cover 优先（带缓存），失败回退纯色。"""
        bg = hexcolor(self._dash_cfg.get("background", "#000000"))
        path = self._resolve_bg_path()
        if not path:
            self._bg_cache, self._bg_cache_key = None, None
            return Image.new("RGB", self.resolution, bg)
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            return Image.new("RGB", self.resolution, bg)
        key = (path, mtime)
        if self._bg_cache_key != key or self._bg_cache is None:
            try:
                with Image.open(path) as f:
                    self._bg_cache = cover_image(f, self.resolution)
                self._bg_cache_key = key
            except Exception as e:
                log.warning("背景图片加载失败 %s: %s", path, e)
                return Image.new("RGB", self.resolution, bg)
        return self._bg_cache.copy()

    def _render(self, metrics: dict) -> None:
        """按组件布局重绘缓存帧，并记录命中矩形。"""
        img = self._background()
        draw = ImageDraw.Draw(img)
        width, _ = self.resolution
        gap = int(self._dash_cfg.get("gap", GAP_Y))
        rects: list[dict] = []
        auto_y = 2
        with self._lock:
            active = list(self._widgets)
            widget_cfgs = self._dash_cfg.get("widgets", [])
        for widget in active:
            cfg = widget.cfg
            cfg_index = next((i for i, c in enumerate(widget_cfgs)
                              if c is cfg), -1)
            h = widget.height(width)
            manual_x, manual_y = cfg.get("x"), cfg.get("y")
            if manual_x is not None or manual_y is not None:
                x = MARGIN_X if manual_x is None else int(manual_x)
                y = auto_y if manual_y is None else int(manual_y)
            else:
                x, y = MARGIN_X, auto_y
                auto_y = y + h + gap
            # 宽度：固定值优先，否则按内容自适应；统一裁到画布右边界内
            max_w = width - x - MARGIN_X
            comp_w = int(cfg["w"]) if cfg.get("w") is not None \
                else widget.natural_width()
            comp_w = max(8, min(comp_w, max_w))
            widget.draw(draw, x, y, comp_w, metrics)
            rects.append({"cfg_index": cfg_index, "type": cfg.get("type"),
                          "x": x, "y": y, "w": comp_w, "h": h})
        self._cache = img
        self._rects = rects

    def draw_frame(self, canvas: Image.Image) -> bool:
        """到刷新间隔就采集重绘；无论与否都把缓存贴到推流 canvas。"""
        now = time.monotonic()
        if now - self._last_sample >= self._refresh:
            try:
                self._render(self._collect_metrics())
            except Exception as e:
                log.warning("仪表盘采集失败: %s", e)
            self._last_sample = now
        canvas.paste(self._cache, (0, 0))
        return True

    def close(self) -> None:
        _temp_monitor.stop()
