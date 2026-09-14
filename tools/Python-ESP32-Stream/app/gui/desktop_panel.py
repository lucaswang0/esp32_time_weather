"""桌面模式面板：显示器选择、自定义区域、裁剪对齐 + 2x 预览。"""
from __future__ import annotations

import tkinter.messagebox as mb

import customtkinter as ctk

from ..sources.desktop_source import DesktopSource, enum_monitors
from .preview import PreviewPlayer


class DesktopPanel(ctk.CTkFrame):
    """桌面捕获配置面板。"""

    def __init__(self, master, cfg: dict, on_apply, **kwargs):
        super().__init__(master, **kwargs)
        self.cfg = cfg
        self._on_apply = on_apply
        self._preview = None
        self.grid_columnconfigure(1, weight=1)
        self._build_controls()
        self._build_preview()

    def _build_controls(self) -> None:
        box = ctk.CTkFrame(self, width=250)
        box.grid(row=0, column=0, sticky="nsew", padx=8, pady=8)
        sc = self.cfg["sources"]["desktop"]
        ctk.CTkLabel(box, text="显示器").pack(anchor="w", padx=10, pady=(10, 0))
        try:
            self._mons = enum_monitors()
        except Exception:
            self._mons = []
        labels = [m["label"] for m in self._mons] or ["主显示器"]
        self._mon_var = ctk.StringVar(value=labels[sc.get("monitor", 0)]
                                      if sc.get("monitor", 0) < len(labels)
                                      else labels[0])
        ctk.CTkOptionMenu(box, values=labels, variable=self._mon_var,
                          command=self._refresh_preview).pack(
            fill="x", padx=10, pady=4)
        ctk.CTkLabel(box, text="裁剪对齐").pack(anchor="w", padx=10, pady=(8, 0))
        self._align = ctk.StringVar(value=sc.get("crop_alignment", "center"))
        ctk.CTkSegmentedButton(box, values=["left", "center", "right"],
                               variable=self._align).pack(fill="x", padx=10)
        self._build_region(box, sc)
        ctk.CTkButton(box, text="应用并切换到桌面",
                      command=self.apply).pack(fill="x", padx=10, pady=14)

    def _build_region(self, box, sc) -> None:
        """自定义物理像素区域开关与输入。"""
        self._use_region = ctk.BooleanVar(value=sc.get("region") is not None)
        ctk.CTkCheckBox(box, text="自定义区域（物理像素）",
                        variable=self._use_region,
                        command=self._refresh_preview).pack(
            anchor="w", padx=10, pady=(12, 2))
        grid = ctk.CTkFrame(box, fg_color="transparent")
        grid.pack(fill="x", padx=10)
        self._rv = {}
        for i, key in enumerate(("left", "top", "width", "height")):
            ctk.CTkLabel(grid, text=key).grid(row=0, column=i * 2, padx=2)
            var = ctk.StringVar(
                value=str(sc["region"].get(key, "")) if sc.get("region") else "")
            ctk.CTkEntry(grid, textvariable=var, width=48).grid(
                row=0, column=i * 2 + 1)
            self._rv[key] = var

    def _build_preview(self) -> None:
        res = (self.cfg["video"]["target_width"],
               self.cfg["video"]["target_height"])
        self._preview = PreviewPlayer(self, res, scale=2)
        self._preview.grid(row=0, column=1, sticky="nsew", padx=8, pady=8)

    def _read_region(self) -> dict | None:
        if not self._use_region.get():
            return None
        try:
            return {k: int(self._rv[k].get().strip())
                    for k in ("left", "top", "width", "height")}
        except ValueError:
            mb.showerror("区域错误", "区域字段必须是整数像素")
            return None

    def _make_source(self) -> DesktopSource:
        res = (self.cfg["video"]["target_width"],
               self.cfg["video"]["target_height"])
        idx = next((m["index"] for m in self._mons
                    if m["label"] == self._mon_var.get()), 0)
        return DesktopSource(res, idx, self._read_region(), self._align.get())

    def _refresh_preview(self, *_args) -> None:
        if self._preview and self._preview.is_running:
            region = self._read_region()
            if self._use_region.get() and region is None:
                return
            self._preview.set_source(self._make_source())

    def apply(self) -> None:
        """写回桌面源配置并通知主窗口切换。"""
        region = self._read_region()
        if self._use_region.get() and region is None:
            return
        sc = self.cfg["sources"]["desktop"]
        sc["monitor"] = next((m["index"] for m in self._mons
                              if m["label"] == self._mon_var.get()), 0)
        sc["region"] = region
        sc["crop_alignment"] = self._align.get()
        self._on_apply("desktop")

    def on_show(self) -> None:
        self._preview.set_source(self._make_source())
        self._preview.start()

    def on_hide(self) -> None:
        self._preview.stop()
