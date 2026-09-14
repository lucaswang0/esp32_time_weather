"""指定窗口面板：窗口搜索/刷新/单选列表、裁剪对齐 + 2x 预览。"""
from __future__ import annotations

import customtkinter as ctk

from ..sources.window_source import WindowSource, enum_windows
from .preview import PreviewPlayer

_MAX_LIST = 60


class WindowPanel(ctk.CTkFrame):
    """窗口捕获配置面板。"""

    def __init__(self, master, cfg: dict, on_apply, **kwargs):
        super().__init__(master, **kwargs)
        self.cfg = cfg
        self._on_apply = on_apply
        self._windows: list[dict] = []
        self._preview = None
        self.grid_columnconfigure(1, weight=1)
        self._build_controls()
        self._build_preview()

    def _build_controls(self) -> None:
        box = ctk.CTkFrame(self, width=280)
        box.grid(row=0, column=0, sticky="nsew", padx=8, pady=8)
        top = ctk.CTkFrame(box, fg_color="transparent")
        top.pack(fill="x", padx=8, pady=(8, 4))
        self._search = ctk.StringVar()
        entry = ctk.CTkEntry(top, textvariable=self._search,
                             placeholder_text="搜索窗口标题")
        entry.pack(side="left", fill="x", expand=True)
        self._search.trace_add("write", lambda *_: self._fill_list())
        ctk.CTkButton(top, text="刷新", width=60,
                      command=self.refresh_list).pack(side="left", padx=6)
        self._list = ctk.CTkScrollableFrame(box, height=300, label_text="")
        self._list.pack(fill="both", expand=True, padx=8, pady=4)
        self._selected = ctk.StringVar(
            value=self.cfg["sources"]["window"].get("window_title", ""))
        align_box = ctk.CTkFrame(box, fg_color="transparent")
        align_box.pack(fill="x", padx=8)
        ctk.CTkLabel(align_box, text="裁剪对齐").pack(side="left")
        self._align = ctk.StringVar(
            value=self.cfg["sources"]["window"].get("crop_alignment", "center"))
        ctk.CTkSegmentedButton(
            align_box, values=["left", "center", "right"],
            variable=self._align, width=160).pack(side="right")
        ctk.CTkButton(box, text="应用并切换到指定窗口",
                      command=self.apply).pack(fill="x", padx=8, pady=10)

    def _build_preview(self) -> None:
        res = (self.cfg["video"]["target_width"],
               self.cfg["video"]["target_height"])
        self._preview = PreviewPlayer(self, res, scale=2)
        self._preview.grid(row=0, column=1, sticky="nsew", padx=8, pady=8)

    def refresh_list(self) -> None:
        """重新枚举顶层窗口。"""
        try:
            self._windows = enum_windows()
        except Exception:
            self._windows = []
        self._fill_list()

    def _fill_list(self) -> None:
        """按搜索词重建窗口单选列表。"""
        for child in self._list.winfo_children():
            child.destroy()
        keyword = self._search.get().lower()
        shown = 0
        for win in self._windows:
            if keyword and keyword not in win["title"].lower():
                continue
            if shown >= _MAX_LIST:
                break
            shown += 1
            text = f"{win['title'][:34]}  {win['width']}x{win['height']}"
            radio = ctk.CTkRadioButton(
                self._list, text=text, variable=self._selected,
                value=win["title"], command=self._refresh_preview)
            radio.pack(anchor="w", pady=2)

    def _make_source(self) -> WindowSource:
        res = (self.cfg["video"]["target_width"],
               self.cfg["video"]["target_height"])
        return WindowSource(res, self._selected.get(), self._align.get())

    def _refresh_preview(self) -> None:
        if self._preview and self._preview.is_running:
            self._preview.set_source(self._make_source())

    def apply(self) -> None:
        """写回窗口配置并通知主窗口切换。"""
        title = self._selected.get().strip()
        if not title:
            return
        self.cfg["sources"]["window"]["window_title"] = title
        self.cfg["sources"]["window"]["crop_alignment"] = self._align.get()
        self._on_apply("window")

    def on_show(self) -> None:
        if not self._windows:
            self.refresh_list()
        self._preview.set_source(self._make_source())
        self._preview.start()

    def on_hide(self) -> None:
        self._preview.stop()
