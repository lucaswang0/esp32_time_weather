"""仪表盘组件：时钟/CPU/内存/磁盘/网速/运行时间/IP/自定义文本。

每个组件实现 height(width) 与 draw(draw,x,y,w,metrics)；
颜色取自配置，字号/进度条随 scale（0.8–2.0）整体缩放。
"""
from __future__ import annotations

import time
from pathlib import Path

import psutil
from PIL import Image, ImageDraw, ImageFont

from .temperatures import TemperatureMonitor

WIDGET_TYPES = ("clock", "cpu", "memory", "disk", "net", "temp",
                "uptime", "ip", "custom_text")

# 温度采集器（由 DashboardSource 注入；未注入时温度组件不显示内容）
_temp_provider: TemperatureMonitor | None = None


def set_temp_provider(monitor: TemperatureMonitor) -> None:
    global _temp_provider
    _temp_provider = monitor

# 画布风格常量
BAR_BG = (42, 47, 56)
MARGIN_X = 4
GAP_Y = 2

_FONT_CANDIDATES = ("msyh.ttc", "segoeui.ttf", "arial.ttf")
_font_cache: dict[int, ImageFont.FreeTypeFont] = {}
# 用于无 canvas 时测量文字宽高的单例 draw
_measure_draw = ImageDraw.Draw(Image.new("RGB", (1, 1)))


def text_width(text: str, font) -> int:
    """测量指定字体下文字的像素宽。"""
    box = _measure_draw.textbbox((0, 0), text, font=font)
    return box[2] - box[0]


def get_font(size: int):
    """按像素字号取字体（缓存）；找不到字体回退 PIL 默认字体。"""
    size = max(7, int(round(size)))
    if size not in _font_cache:
        fonts_dir = Path("C:/Windows/Fonts")
        font = None
        for name in _FONT_CANDIDATES:
            p = fonts_dir / name
            if p.exists():
                try:
                    font = ImageFont.truetype(str(p), size)
                    break
                except Exception:
                    pass
        _font_cache[size] = font or ImageFont.load_default()
    return _font_cache[size]


def hexcolor(text: str, fallback=(255, 255, 255)) -> tuple[int, int, int]:
    """#RRGGBB -> RGB 元组，非法格式用 fallback。"""
    try:
        t = text.lstrip("#")
        return (int(t[0:2], 16), int(t[2:4], 16), int(t[4:6], 16))
    except Exception:
        return fallback


def _draw_bar(draw: ImageDraw.ImageDraw, x, y, w, h, ratio,
              color: tuple[int, int, int]) -> None:
    """绘制水平进度条（无圆角，小屏上直角更清晰）。"""
    ratio = max(0.0, min(1.0, ratio))
    draw.rectangle((x, y, x + w, y + h), fill=BAR_BG)
    if ratio > 0 and w > 0:
        draw.rectangle((x, y, x + int(w * ratio), y + h), fill=color)


def _draw_sparkline(draw, x, y, w, h, history,
                    color: tuple[int, int, int]) -> None:
    """历史曲线（0..100 映射到高度，样本不足不画）。"""
    data = list(history)[-w:] if w > 2 else []
    if len(data) < 2:
        return
    step = w / max(1, len(data) - 1)
    points = [(x + int(i * step),
               y + h - int(max(0, min(100, v)) / 100.0 * h))
              for i, v in enumerate(data)]
    draw.line(points, fill=color, width=1)


def fmt_bytes(num: float) -> str:
    """字节数自适应单位（磁盘容量/内存）。"""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(num) < 1024:
            return f"{num:.0f}{unit}" if unit == "B" else f"{num:.1f}{unit}"
        num /= 1024
    return f"{num:.1f}PB"


def fmt_rate(bps: float) -> str:
    """字节/秒自适应单位（网速）。"""
    return fmt_bytes(bps) + "/s"


class Widget:
    """组件基类：持有配置、颜色与缩放倍率。"""

    BASE_HEIGHT = 16

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.color = hexcolor(cfg.get("color", "#FFFFFF"))
        self.scale = float(cfg.get("scale", 1.0) or 1.0)

    def _sz(self, base: int) -> int:
        """基准像素 × 缩放倍率。"""
        return max(1, int(round(base * self.scale)))

    def font(self, base: int):
        return get_font(base * self.scale)

    def height(self, width: int) -> int:
        return self._sz(self.BASE_HEIGHT)

    def natural_width(self) -> int:
        """按内容估算宽度（不依赖实时指标），使组件不再占满整行。"""
        return self._sz(80)

    def draw(self, draw: ImageDraw.ImageDraw, x, y, w, metrics: dict) -> None:
        raise NotImplementedError


class ClockWidget(Widget):
    """日期小字 + 时间大字。"""

    BASE_HEIGHT = 30

    def natural_width(self) -> int:
        return max(text_width("2026-09-12 Sat", self.font(10)),
                   text_width("00:00:00", self.font(16)))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        now = metrics["now"]
        draw.text((x, y), time.strftime("%Y-%m-%d %a", now),
                  fill=(170, 170, 170), font=self.font(10))
        draw.text((x, y + self._sz(12)), time.strftime("%H:%M:%S", now),
                  fill=self.color, font=self.font(16))


class CpuWidget(Widget):
    """CPU 百分比 + 进度条或历史曲线。"""

    BASE_HEIGHT = 26

    def natural_width(self) -> int:
        return max(text_width("CPU 100%", self.font(12)), self._sz(96))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        pct = metrics["cpu"]
        draw.text((x, y), f"CPU {pct:4.0f}%", fill=self.color,
                  font=self.font(12))
        by, bh = y + self._sz(15), max(3, self._sz(9))
        if self.cfg.get("sparkline"):
            draw.rectangle((x, by, x + w, by + bh), fill=BAR_BG)
            _draw_sparkline(draw, x, by, w, bh, metrics["cpu_history"],
                            self.color)
        else:
            _draw_bar(draw, x, by, w, bh, pct / 100.0, self.color)


class MemoryWidget(Widget):
    """内存使用率 + 已用/总量。"""

    BASE_HEIGHT = 22

    def natural_width(self) -> int:
        return max(text_width("MEM 100%  999.9GB/999.9GB", self.font(12)),
                   self._sz(120))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        m = metrics["mem"]
        label = f"MEM {m['percent']:4.0f}%  {fmt_bytes(m['used'])}/{fmt_bytes(m['total'])}"
        draw.text((x, y), label, fill=self.color, font=self.font(12))
        _draw_bar(draw, x, y + self._sz(14), w, max(2, self._sz(6)),
                  m["percent"] / 100.0, self.color)


class DiskWidget(Widget):
    """指定分区使用率。"""

    BASE_HEIGHT = 22

    def natural_width(self) -> int:
        drive = self.cfg.get("path", "C:\\")
        sample = f"{drive} 100%  999.9GB/999.9GB"
        return max(text_width(sample, self.font(12)), self._sz(120))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        path = self.cfg.get("path", "C:\\").upper()
        info = metrics["disks"].get(path)
        if info is None:
            draw.text((x, y), "DISK N/A", fill=(160, 80, 80),
                      font=self.font(12))
            return
        label = (f"{self.cfg.get('path', '')} {info['percent']:4.0f}%  "
                 f"{fmt_bytes(info['used'])}/{fmt_bytes(info['total'])}")
        draw.text((x, y), label, fill=self.color, font=self.font(12))
        _draw_bar(draw, x, y + self._sz(14), w, max(2, self._sz(6)),
                  info["percent"] / 100.0, self.color)


class NetWidget(Widget):
    """上行/下行速率，可选指定网卡。"""

    def natural_width(self) -> int:
        return text_width("NET d:999.9KB/s u:999.9KB/s", self.font(12))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        draw.text((x, y),
                  f"NET d:{fmt_rate(metrics['net_down'])} "
                  f"u:{fmt_rate(metrics['net_up'])}",
                  fill=self.color, font=self.font(12))


class TempWidget(Widget):
    """传感器组件：按勾选的传感器多行显示（温度/电压/风扇/功耗/频率/负载等）。"""

    _ROW_BASE = 14

    def _enabled_sensors(self) -> list[dict]:
        """读取提供方当前传感器并按 temp_enabled 过滤（缺省=启用）。"""
        if _temp_provider is None:
            return []
        enabled = self.cfg.get("temp_enabled") or {}
        return [s for s in _temp_provider.scan()
                if enabled.get(s["key"], True)]

    def height(self, width: int) -> int:
        return max(1, len(self._enabled_sensors())) * self._sz(self._ROW_BASE)

    def natural_width(self) -> int:
        sensors = self._enabled_sensors() or [{"label": "显卡 ", "unit": "°C"}]
        return max(text_width(f"{s['label']} 100{s.get('unit', '')}",
                              self.font(12)) for s in sensors)

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        row_h = self._sz(self._ROW_BASE)
        sensors = self._enabled_sensors()
        if not sensors:
            draw.text((x, y), "SENSOR --", fill=(120, 120, 120),
                      font=self.font(12))
            return
        for i, s in enumerate(sensors):
            value = s["value"]
            unit = s.get("unit", "")
            text = f"{s['label']} {value}{unit}" if value is not None \
                else f"{s['label']} N/A"
            draw.text((x, y + i * row_h), text, fill=self.color,
                      font=self.font(12))


class UptimeWidget(Widget):
    """系统开机运行时长。"""

    def natural_width(self) -> int:
        return text_width("UP 365d 23h 59m", self.font(12))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        secs = int(time.time() - metrics["boot_time"])
        days, rem = divmod(secs, 86400)
        hours, rem = divmod(rem, 3600)
        mins = rem // 60
        draw.text((x, y), f"UP {days}d {hours}h {mins}m",
                  fill=self.color, font=self.font(12))


class IpWidget(Widget):
    """主机名 + 本机 IPv4。"""

    def natural_width(self) -> int:
        return text_width("255.255.255.255", self.font(12))

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        draw.text((x, y), f"{metrics['hostname']} {metrics['ip']}",
                  fill=self.color, font=self.font(12))


class CustomTextWidget(Widget):
    """用户自定义静态文本。"""

    def natural_width(self) -> int:
        text = self.cfg.get("text", "")
        return text_width(text, self.font(12)) if text else self._sz(40)

    def draw(self, draw, x, y, w, metrics: dict) -> None:
        draw.text((x, y), self.cfg.get("text", ""),
                  fill=self.color, font=self.font(12))


_REGISTRY = {
    "clock": ClockWidget, "cpu": CpuWidget, "memory": MemoryWidget,
    "disk": DiskWidget, "net": NetWidget, "temp": TempWidget,
    "uptime": UptimeWidget, "ip": IpWidget, "custom_text": CustomTextWidget,
}


def build_widget(cfg: dict) -> Widget | None:
    """按配置构造组件，未知类型返回 None。"""
    cls = _REGISTRY.get(cfg.get("type"))
    return cls(cfg) if cls else None


def list_disk_paths() -> list[str]:
    """枚举固定磁盘盘符（Windows opts 含 fixed，去重大写）。"""
    paths = []
    for p in psutil.disk_partitions(all=False):
        drive = p.mountpoint.upper()
        if "fixed" in p.opts.lower() and drive not in paths:
            paths.append(drive)
    return paths or ["C:\\"]


def list_net_adapters() -> list[str]:
    """枚举有流量统计的网卡名（排除环回）。"""
    stats = psutil.net_io_counters(pernic=True)
    return [name for name in stats if not name.startswith(("loopback", "lo"))]
