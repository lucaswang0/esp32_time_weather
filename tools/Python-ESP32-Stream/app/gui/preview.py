"""帧源 2x 预览：独立 FrameSource + after 定时刷新 PIL→CTkImage。

可选交互：set_handlers 后预览图把鼠标事件换算为画布坐标回调
（down/move/up），供仪表盘组件拖拽使用；annotate 钩子在显示前
对画布图像做最后标注（如选中框）。
"""
from __future__ import annotations

import logging

import customtkinter as ctk
from PIL import Image

from ..sources.base import FrameSource

log = logging.getLogger(__name__)


class PreviewPlayer(ctk.CTkFrame):
    """源预览控件；set_source 热替换预览源，不影响正在推流的源。

    显示缩放按容器实际空间自适应（不超过 max_scale），图像左对齐锚定，
    避免分栏变窄时图像被居中裁剪导致左侧组件不可见。
    """

    def __init__(self, master, resolution: tuple[int, int], scale: int = 2,
                 interval_ms: int = 500, **kwargs):
        super().__init__(master, **kwargs)
        self._res = resolution
        self._max_scale = scale
        self._scale = float(scale)
        self._interval = interval_ms
        self._source: FrameSource | None = None
        self._running = False
        self._after_id = None
        self._img = None
        self._handlers = None
        self._annotate = None
        size = (resolution[0] * scale, resolution[1] * scale)
        self._label = ctk.CTkLabel(self, text="预览未启动", width=size[0],
                                   height=size[1], fg_color="#000000")
        # anchor="w"：空间不足时左边缘固定，只裁右侧
        self._label.pack(padx=6, pady=(6, 2), anchor="w")
        self._hint = ctk.CTkLabel(self, text="", text_color="#FF8A65")
        self._hint.pack(anchor="w", padx=6)
        self.bind("<Configure>", self._on_resize)

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def source(self) -> FrameSource | None:
        return self._source

    def set_handlers(self, on_down=None, on_move=None, on_up=None) -> None:
        """注册画布坐标鼠标回调；传 None 取消交互。

        注意：CTkLabel.bind 会自动路由到内部承载图像的 tk.Label（实际落点），
        因此只绑定一次，避免双触发。
        """
        self._handlers = (on_down, on_move, on_up)
        if on_down or on_move or on_up:
            self._label.bind("<Button-1>", self._evt_down)
            self._label.bind("<B1-Motion>", self._evt_move)
            self._label.bind("<ButtonRelease-1>", self._evt_up)
            self._label.configure(cursor="hand2")
        else:
            for seq in ("<Button-1>", "<B1-Motion>", "<ButtonRelease-1>"):
                self._label.unbind(seq)
            self._label.configure(cursor="arrow")

    def set_annotate(self, fn) -> None:
        """注册标注回调 fn(PIL.Image)，在每帧显示前调用。"""
        self._annotate = fn

    def start(self) -> None:
        if not self._running:
            self._running = True
            self._tick()

    def stop(self) -> None:
        """停止刷新并关闭预览源。"""
        self._running = False
        if self._after_id:
            self.after_cancel(self._after_id)
            self._after_id = None
        self._close_source()

    def set_source(self, source: FrameSource) -> None:
        """替换预览源（旧源关闭，新源在主线程外资源按需打开）。"""
        self._close_source()
        self._source = source
        try:
            source.open()
            self._hint.configure(text="")
        except Exception as e:
            log.warning("预览源打开失败: %s", e)
            self._hint.configure(text=f"预览源不可用: {e}")

    def refresh_now(self) -> None:
        """立即重绘一帧（拖拽时保证位置即时反馈）。"""
        if self._running:
            self._render_once()

    def _on_resize(self, event=None) -> None:
        """容器尺寸变化时按可用空间重算显示缩放（向下取整到 0.1）。"""
        try:
            ws = self._get_widget_scaling()
        except Exception:
            ws = 1.0
        # winfo/event 为物理像素，换算为 CTk 基准单位
        avail_w = self.winfo_width() / ws - 12
        avail_h = self.winfo_height() / ws - 40
        if avail_w <= 1 or avail_h <= 1:
            return
        eff = min(self._max_scale,
                  avail_w / self._res[0], avail_h / self._res[1])
        eff = max(0.5, (int(eff * 10) / 10.0))
        if abs(eff - self._scale) < 0.01:
            return
        self._scale = eff
        self._label.configure(width=int(self._res[0] * eff),
                              height=int(self._res[1] * eff))
        self._img = None  # 强制按新尺寸重建 CTkImage
        self.refresh_now()

    def _close_source(self) -> None:
        if self._source:
            try:
                self._source.close()
            except Exception:
                log.exception("关闭预览源异常")
            self._source = None

    def _to_canvas(self, event) -> tuple[int, int]:
        """事件物理坐标 -> 画布坐标（扣除控件 DPI 缩放与控件原点差）。"""
        try:
            ws = self._get_widget_scaling()
        except Exception:
            ws = 1.0
        # 事件可能来自内部 tk.Label：把它的原点修正到 CTkLabel 原点（基准单位）
        src = event.widget
        if src is not self._label and hasattr(src, "winfo_rootx"):
            ox = (src.winfo_rootx() - self._label.winfo_rootx()) / ws
            oy = (src.winfo_rooty() - self._label.winfo_rooty()) / ws
        else:
            ox = oy = 0.0
        cx = int((event.x / ws - ox) / self._scale)
        cy = int((event.y / ws - oy) / self._scale)
        cx = max(0, min(self._res[0] - 1, cx))
        cy = max(0, min(self._res[1] - 1, cy))
        return cx, cy

    def _evt_down(self, event) -> None:
        if self._handlers and self._handlers[0]:
            self._handlers[0](*self._to_canvas(event))

    def _evt_move(self, event) -> None:
        if self._handlers and self._handlers[1]:
            self._handlers[1](*self._to_canvas(event))

    def _evt_up(self, event) -> None:
        if self._handlers and self._handlers[2]:
            self._handlers[2](*self._to_canvas(event))

    def _tick(self) -> None:
        if not self._running:
            return
        self._render_once()
        self._after_id = self.after(self._interval, self._tick)

    def _render_once(self) -> None:
        """抓一帧送显；源暂不可用时提示。"""
        if self._source is None:
            return
        self._label.configure(text="")
        canvas = Image.new("RGB", self._res, (0, 0, 0))
        try:
            ok = self._source.draw_frame(canvas)
        except Exception as e:
            self._hint.configure(text=f"取帧失败: {e}")
            return
        if not ok:
            self._hint.configure(text="源暂不可用（窗口不存在/已最小化）")
            return
        self._hint.configure(text="")
        if self._annotate:
            try:
                self._annotate(canvas)
            except Exception:
                log.exception("预览标注失败")
        self._img = ctk.CTkImage(
            light_image=canvas, dark_image=canvas,
            size=(self._res[0] * self._scale, self._res[1] * self._scale))
        self._label.configure(image=self._img, text="")
