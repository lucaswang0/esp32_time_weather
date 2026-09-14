"""系统托盘：驻留隐藏、启停/切换菜单、连接状态圆点图标、气泡提示。

pystray 在自己的线程跑 Win32 消息循环；菜单回调只投递命令队列，
由 tk 主线程 after 泵执行，杜绝跨线程操作 tk。
"""
from __future__ import annotations

import logging
import queue

from PIL import Image, ImageDraw
from pystray import Icon, Menu, MenuItem

log = logging.getLogger(__name__)

# 连接状态 -> 圆点颜色
_STATE_COLORS = {
    "idle": (120, 120, 120, 255),
    "connecting": (255, 193, 7, 255),
    "connected": (0, 230, 118, 255),
    "disconnected": (255, 82, 82, 255),
}
_STATE_TEXT = {"idle": "未连接", "connecting": "连接中",
               "connected": "已连接", "disconnected": "重连中"}

_SOURCES = (("桌面", "desktop"), ("指定窗口", "window"), ("仪表盘", "dashboard"))


def _make_icon_image(state: str, size: int = 64) -> Image.Image:
    """用 Pillow 画一个彩色圆点托盘图标，无需外部 ico 资源。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    color = _STATE_COLORS.get(state, _STATE_COLORS["idle"])
    draw.ellipse((6, 6, size - 6, size - 6), fill=color)
    return img


class TrayController:
    """托盘生命周期与菜单；get_state/get_mode 由应用注入。"""

    def __init__(self, command_queue: queue.Queue, get_state, get_mode,
                 get_streaming):
        self._q = command_queue
        self._get_state = get_state
        self._get_mode = get_mode
        self._get_streaming = get_streaming
        self._icon: Icon | None = None

    def start(self) -> None:
        """构造图标与动态菜单并在独立线程运行。"""
        self._icon = Icon(
            "ESP32Stream", icon=_make_icon_image("idle"),
            title="ESP32 Stream — 未启动", menu=self._build_menu())
        self._icon.run_detached()

    def stop(self) -> None:
        if self._icon:
            self._icon.stop()
            self._icon = None

    def notify_hidden(self) -> None:
        """首次最小化到托盘时的气泡提示。"""
        if self._icon:
            self._icon.notify("ESP32 推流仍在后台运行，双击图标返回主界面",
                              title="已最小化到托盘")

    def update(self, conn_state: str, streaming: bool, fps: float = 0.0) -> None:
        """更新圆点颜色与 tooltip（主线程调用，pystray 内部异步刷新）。"""
        if not self._icon:
            return
        self._icon.icon = _make_icon_image(conn_state)
        text = _STATE_TEXT.get(conn_state, "")
        suffix = f" · {fps:.0f}fps" if streaming and conn_state == "connected" else ""
        self._icon.title = f"ESP32 Stream — {text}{suffix}"

    def _post(self, command: str) -> None:
        try:
            self._q.put_nowait(command)
        except queue.Full:
            log.warning("托盘命令队列已满: %s", command)

    def _build_menu(self) -> Menu:
        """构造菜单（pystray 每次展开会重新求值 checked/text）。"""
        show = MenuItem("显示主界面", lambda *_: self._post("show"),
                        default=True)
        toggle = MenuItem(self._toggle_text,
                          lambda *_: self._post("toggle"))
        sub = Menu(*self._source_menu())
        return Menu(show, Menu.SEPARATOR, toggle,
                    MenuItem("画面源", sub), Menu.SEPARATOR,
                    MenuItem("退出", lambda *_: self._post("quit")))

    def _source_menu(self):
        for text, mode in _SOURCES:
            checked = lambda item, m=mode: self._get_mode() == m
            yield MenuItem(text, lambda _, m=mode: self._post(f"switch:{m}"),
                           radio=True, checked=checked)

    def _toggle_text(self, icon=None, item=None) -> str:
        return "停止推流" if self._get_streaming() else "开始推流"
