"""仪表盘面板：组件启用/排序/颜色/缩放/定位编辑 + 背景（纯色/图片）+ 2x 拖拽预览。"""
from __future__ import annotations

import os
from copy import deepcopy

import customtkinter as ctk
import tkinter.colorchooser as cc
import tkinter.filedialog as fd
from PIL import ImageDraw

from ..sources.dashboard_source import (DashboardSource,
                                        compute_layout_height, _temp_monitor)
from ..sources.widgets import list_disk_paths, list_net_adapters
from .preview import PreviewPlayer

_TYPE_LABEL = {"clock": "时钟", "cpu": "CPU", "memory": "内存",
               "disk": "磁盘", "net": "网速", "temp": "传感器",
               "uptime": "运行时间", "ip": "IP地址", "custom_text": "自定义文本"}
_SCALE_MIN, _SCALE_MAX, _SCALE_STEP = 0.8, 2.0, 0.1


class DashboardPanel(ctk.CTkFrame):
    """仪表盘组件编辑面板。"""

    def __init__(self, master, cfg: dict, on_apply, **kwargs):
        super().__init__(master, **kwargs)
        self.cfg = cfg
        self._on_apply = on_apply
        d = cfg["sources"]["dashboard"]
        self._widgets = deepcopy(d["widgets"])
        self._bg = d.get("background", "#0B0F14")
        self._bg_image = d.get("bg_image")
        self._gap = int(d.get("gap", 2))
        self._drives = list_disk_paths()
        self._adapters = list_net_adapters()
        self._pos_vars: dict[int, tuple] = {}
        self._scale_vars: dict[int, ctk.StringVar] = {}
        self._preview = None
        self._selected = None
        self._drag = None
        self._temp_poll_id = None
        self._temp_rendered_keys: list[str] = []
        self._temp_list = None
        self._temp_hint = None
        self.grid_columnconfigure(1, weight=1)
        self._build_editor()
        self._build_preview()

    # ---------- 左侧编辑器 ----------

    def _build_editor(self) -> None:
        box = ctk.CTkFrame(self, width=330)
        box.grid(row=0, column=0, sticky="nsew", padx=8, pady=8)
        self._cards = ctk.CTkScrollableFrame(box, height=280)
        self._cards.pack(fill="both", expand=True, padx=6, pady=6)
        self._warn = ctk.CTkLabel(box, text="", text_color="#FF5252")
        self._warn.pack(anchor="w", padx=10)
        self._build_global_row(box)
        self._build_cards()

    def _build_cards(self) -> None:
        """重建全部组件卡片。"""
        self._pos_vars.clear()
        self._scale_vars.clear()
        self._temp_rendered_keys = []
        for child in self._cards.winfo_children():
            child.destroy()
        for i, w in enumerate(self._widgets):
            self._build_card(i, w)
        self._update_warning()

    def _build_card(self, i: int, w: dict) -> None:
        card = ctk.CTkFrame(self._cards)
        card.pack(fill="x", pady=3)
        var = ctk.BooleanVar(value=w.get("enabled", True))
        ctk.CTkCheckBox(card, text=_TYPE_LABEL.get(w["type"], w["type"]),
                        variable=var,
                        command=lambda: self._toggle(i, var)).grid(
            row=0, column=0, padx=6, pady=4, sticky="w")
        self._color_button(card, i, w).grid(row=0, column=1, padx=4)
        ctk.CTkButton(card, text="↑", width=28,
                      command=lambda: self._move(i, -1)).grid(row=0, column=2)
        ctk.CTkButton(card, text="↓", width=28,
                      command=lambda: self._move(i, 1)).grid(
            row=0, column=3, padx=(0, 6))
        self._build_card_option(card, i, w)
        self._build_layout_row(card, i, w)

    def _color_button(self, parent, i: int, w: dict) -> ctk.CTkButton:
        return ctk.CTkButton(parent, text=w.get("color", "#FFFFFF"), width=80,
                             fg_color=w.get("color", "#FFFFFF"),
                             command=lambda: self._pick_color(i))

    def _build_card_option(self, card, i: int, w: dict) -> None:
        """组件特有参数行。"""
        if w["type"] == "cpu":
            var = ctk.BooleanVar(value=w.get("sparkline", False))
            ctk.CTkCheckBox(card, text="曲线图", variable=var,
                            command=lambda: self._set(i, "sparkline", var.get())
                            ).grid(row=1, column=0, columnspan=2, padx=6,
                                   pady=(0, 2), sticky="w")
        elif w["type"] == "disk":
            self._option_menu(card, self._drives, w.get("path", "C:\\"),
                              lambda v: self._set(i, "path", v))
        elif w["type"] == "net":
            values = ["(全部网卡)"] + self._adapters
            self._option_menu(card, values, w.get("adapter") or "(全部网卡)",
                              lambda v: self._set(i, "adapter", None
                              if v == "(全部网卡)" else v))
        elif w["type"] == "custom_text":
            var = ctk.StringVar(value=w.get("text", ""))
            entry = ctk.CTkEntry(card, textvariable=var, width=190,
                                 placeholder_text="自定义文本")
            entry.grid(row=1, column=0, columnspan=4, padx=6, pady=(0, 2),
                       sticky="w")
            var.trace_add("write", lambda *_: self._set(i, "text", var.get()))
        elif w["type"] == "temp":
            self._build_temp_option(card, w)

    def _build_layout_row(self, card, i: int, w: dict) -> None:
        """紧凑布局行：自动 | x y 宽 | 缩放 −/+（单行 grid 不占多行）。"""
        row = ctk.CTkFrame(card, fg_color="transparent")
        row.grid(row=2, column=0, columnspan=4, sticky="w", padx=2, pady=(0, 4))
        manual = w.get("x") is not None or w.get("y") is not None
        auto_var = ctk.BooleanVar(value=not manual)
        x_var = ctk.StringVar(value="" if w.get("x") is None else str(w["x"]))
        y_var = ctk.StringVar(value="" if w.get("y") is None else str(w["y"]))
        w_var = ctk.StringVar(value="" if w.get("w") is None else str(w["w"]))
        self._pos_vars[i] = (auto_var, x_var, y_var, w_var)
        ctk.CTkCheckBox(row, text="自动", variable=auto_var, width=46,
                        command=lambda: self._toggle_auto(i)).grid(
            row=0, column=0, padx=(2, 2))
        col = 1
        for label, var in (("x", x_var), ("y", y_var), ("宽", w_var)):
            ctk.CTkLabel(row, text=label, width=10).grid(row=0, column=col)
            entry = ctk.CTkEntry(row, textvariable=var, width=34)
            entry.grid(row=0, column=col + 1, padx=(0, 2))
            entry.bind("<FocusOut>", lambda *_: self._apply_layout(i))
            entry.bind("<Return>", lambda *_: self._apply_layout(i))
            col += 2
        ctk.CTkButton(row, text="−", width=22,
                      command=lambda: self._bump_scale(i, -1)).grid(
            row=0, column=col, padx=(6, 0))
        scale_var = ctk.StringVar(value=f"{float(w.get('scale', 1.0)):.1f}")
        self._scale_vars[i] = scale_var
        ctk.CTkLabel(row, textvariable=scale_var, width=28).grid(
            row=0, column=col + 1)
        ctk.CTkButton(row, text="+", width=22,
                      command=lambda: self._bump_scale(i, 1)).grid(
            row=0, column=col + 2)

    def _option_menu(self, parent, values, current, callback) -> None:
        var = ctk.StringVar(value=current if current in values else values[0])
        ctk.CTkOptionMenu(parent, values=values, variable=var, width=190,
                          command=callback).grid(
            row=1, column=0, columnspan=4, padx=6, pady=(0, 2), sticky="w")

    # ---------- 温度传感器 ----------

    def _temp_cfg(self):
        return next((w for w in self._widgets if w["type"] == "temp"), None)

    def _build_temp_option(self, card, w: dict) -> None:
        """温度卡片：扫描按钮 + 状态 + 传感器勾选列表。"""
        box = ctk.CTkFrame(card, fg_color="transparent")
        box.grid(row=1, column=0, columnspan=4, sticky="ew", padx=6,
                 pady=(0, 2))
        top = ctk.CTkFrame(box, fg_color="transparent")
        top.pack(fill="x")
        ctk.CTkButton(top, text="扫描传感器", width=120,
                      command=self._kick_temp_scan).pack(side="left")
        self._temp_hint = ctk.CTkLabel(top, text="等待扫描…",
                                       text_color="#FFC107", anchor="w")
        self._temp_hint.pack(side="left", padx=8, fill="x", expand=True)
        self._temp_list = ctk.CTkFrame(box, fg_color="transparent")
        self._temp_list.pack(fill="x", anchor="w")
        self._temp_rendered_keys: list[str] = []

    def _kick_temp_scan(self) -> None:
        source = self._preview.source if self._preview else None
        if isinstance(source, DashboardSource):
            source.kick_temp_scan()
        self._temp_hint.configure(text="扫描中…（首次约 2-3 秒）",
                                  text_color="#FFC107")

    def _poll_temp(self) -> None:
        """面板可见时每秒同步温度传感器勾选列表（键集合不变不重建）。"""
        source = self._preview.source if self._preview else None
        if isinstance(source, DashboardSource) and self._temp_hint:
            sensors = source.temp_sensors
            keys = [s["key"] for s in sensors]
            if keys != self._temp_rendered_keys:
                self._render_temp_sensors(sensors)
            if keys:
                self._temp_hint.configure(
                    text=f"{len(keys)} 个传感器（温度/电压/风扇/功耗等；"
                         "CPU温度/主板电压需以管理员运行本程序）",
                    text_color="#8AB4F8")
            elif _temp_monitor.has_scanned:
                self._temp_hint.configure(
                    text="未发现传感器：显卡需 NVIDIA 驱动；CPU/主板/硬盘温度需以"
                         "管理员运行本程序以加载 LibreHardwareMonitor",
                    text_color="#FF5252")
        if self._temp_poll_id is not None:
            self._temp_poll_id = self.after(1000, self._poll_temp)

    def _render_temp_sensors(self, sensors: list[dict]) -> None:
        """重建传感器复选框（选中状态存 temp widget 的 temp_enabled）。"""
        for child in self._temp_list.winfo_children():
            child.destroy()
        self._temp_rendered_keys = [s["key"] for s in sensors]
        cfg = self._temp_cfg()
        if cfg is None:
            return
        enabled = cfg.setdefault("temp_enabled", {})
        for s in sensors:
            var = ctk.BooleanVar(value=enabled.get(s["key"], True))
            unit = s.get("unit", "")
            text = f"{s['label']} {s['value']}{unit}" if s["value"] is not None \
                else f"{s['label']} N/A"

            def toggle(key=s["key"], v=var):
                enabled[key] = bool(v.get())
                self._sync_preview()
            ctk.CTkCheckBox(self._temp_list, text=text, variable=var,
                            command=toggle).pack(anchor="w", padx=2, pady=1)

    def _build_global_row(self, box) -> None:
        row = ctk.CTkFrame(box, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=(0, 6))
        ctk.CTkLabel(row, text="背景").pack(side="left")
        self._bg_btn = ctk.CTkButton(row, text=self._bg, width=80,
                                     fg_color=self._bg,
                                     command=self._pick_background)
        self._bg_btn.pack(side="left", padx=4)
        ctk.CTkButton(row, text="图片…", width=60,
                      command=self._pick_bg_image).pack(side="left", padx=2)
        self._img_lbl = ctk.CTkLabel(row, text=self._img_short(), width=90,
                                     anchor="w", text_color="#8AB4F8")
        self._img_lbl.pack(side="left", padx=2)
        ctk.CTkButton(row, text="清除图", width=56,
                      command=self._clear_bg_image).pack(side="left")
        row2 = ctk.CTkFrame(box, fg_color="transparent")
        row2.pack(fill="x", padx=8, pady=(0, 6))
        ctk.CTkLabel(row2, text="刷新s").pack(side="left")
        self._refresh = ctk.StringVar(
            value=str(self.cfg["sources"]["dashboard"]
                      .get("refresh_interval_sec", 1.0)))
        refresh_entry = ctk.CTkEntry(row2, textvariable=self._refresh, width=46)
        refresh_entry.pack(side="left", padx=4)
        ctk.CTkLabel(row2, text="间距").pack(side="left", padx=(8, 2))
        self._gap_var = ctk.StringVar(value=str(self._gap))
        gap_entry = ctk.CTkEntry(row2, textvariable=self._gap_var, width=40)
        gap_entry.pack(side="left")
        gap_entry.bind("<FocusOut>", lambda *_: self._apply_gap())
        gap_entry.bind("<Return>", lambda *_: self._apply_gap())
        ctk.CTkButton(row2, text="自动排列全部", width=100,
                      command=self._auto_arrange).pack(side="left", padx=8)
        ctk.CTkButton(box, text="保存并切换到仪表盘",
                      command=self.apply).pack(fill="x", padx=8, pady=(0, 8))

    def _img_short(self) -> str:
        if not self._bg_image:
            return "（纯色）"
        return os.path.basename(self._bg_image)[:14]

    def _build_preview(self) -> None:
        res = (self.cfg["video"]["target_width"],
               self.cfg["video"]["target_height"])
        self._preview = PreviewPlayer(self, res, scale=2)
        self._preview.grid(row=0, column=1, sticky="nsew", padx=8, pady=8)
        self._preview.set_handlers(self._drag_down, self._drag_move, None)
        self._preview.set_annotate(self._annotate)

    # ---------- 拖拽 ----------

    def _hit_test(self, cx: int, cy: int):
        """命中最上层组件矩形，返回 (cfg_index, rect)。"""
        source = self._preview.source
        if not isinstance(source, DashboardSource):
            return None
        for rect in reversed(source.get_rects()):
            if (rect["x"] <= cx <= rect["x"] + rect["w"]
                    and rect["y"] <= cy <= rect["y"] + rect["h"]):
                return rect["cfg_index"], rect
        return None

    _HANDLE = 5  # 右边缘拉伸手柄热区（画布像素）

    def _find_rect(self, idx: int):
        source = self._preview.source
        if not isinstance(source, DashboardSource):
            return None
        for rect in source.get_rects():
            if rect["cfg_index"] == idx:
                return rect
        return None

    def _hit_resize(self, rect, cx: int, cy: int) -> bool:
        """是否点中组件右边缘拉伸手柄。"""
        return (rect["x"] + rect["w"] - self._HANDLE <= cx
                <= rect["x"] + rect["w"] + self._HANDLE
                and rect["y"] <= cy <= rect["y"] + rect["h"])

    def _drag_down(self, cx: int, cy: int) -> None:
        # 已选中组件的右边缘优先判定为拉伸
        if self._selected is not None:
            sel_rect = self._find_rect(self._selected)
            if sel_rect and self._hit_resize(sel_rect, cx, cy):
                self._drag = {"idx": self._selected, "mode": "resize"}
                if self._widgets[self._selected].get("w") is None:
                    self._widgets[self._selected]["w"] = sel_rect["w"]
                return
        hit = self._hit_test(cx, cy)
        if not hit:
            self._selected = None
            self._preview.refresh_now()
            return
        idx, rect = hit
        self._selected = idx
        if self._hit_resize(rect, cx, cy):
            self._drag = {"idx": idx, "mode": "resize"}
            if self._widgets[idx].get("w") is None:
                self._widgets[idx]["w"] = rect["w"]
        else:
            self._drag = {"idx": idx, "mode": "move",
                          "ox": cx - rect["x"], "oy": cy - rect["y"]}
            # 开始拖动即转为绝对定位
            self._widgets[idx]["x"], self._widgets[idx]["y"] = rect["x"], rect["y"]
        self._sync_pos_vars(idx)
        self._sync_preview()

    def _drag_move(self, cx: int, cy: int) -> None:
        if not self._drag:
            return
        idx = self._drag["idx"]
        w = self._widgets[idx]
        cw, ch = (self.cfg["video"]["target_width"],
                  self.cfg["video"]["target_height"])
        if self._drag["mode"] == "resize":
            rect = self._find_rect(idx)
            base_x = rect["x"] if rect else 0
            w["w"] = max(12, min(cw - base_x - 4, cx - base_x + self._HANDLE))
        else:
            w["x"] = max(0, min(cw - 8, cx - self._drag["ox"]))
            w["y"] = max(0, min(ch - 8, cy - self._drag["oy"]))
        self._sync_pos_vars(idx)
        self._sync_preview()
        self._preview.refresh_now()

    def _annotate(self, image) -> None:
        """选中组件画黄框 + 右边缘拉伸手柄。"""
        if self._selected is None:
            return
        rect = self._find_rect(self._selected)
        if not rect:
            return
        draw = ImageDraw.Draw(image)
        x0, y0 = rect["x"], rect["y"]
        x1, y1 = x0 + rect["w"] - 1, y0 + rect["h"] - 1
        draw.rectangle((x0, y0, x1, y1), outline=(255, 213, 0))
        draw.rectangle((x1 - 2, y0 - 1, x1 + 2, y1 + 1), fill=(255, 213, 0))

    # ---------- 编辑动作 ----------

    def _temp_dash_cfg(self) -> dict:
        return {"background": self._bg, "bg_image": self._bg_image,
                "refresh_interval_sec": 1.0, "gap": self._gap,
                "widgets": self._widgets}

    def _apply_gap(self) -> None:
        """间距输入：0–50 整数，非法回退当前值。"""
        text = self._gap_var.get().strip()
        if text.isdigit():
            self._gap = min(50, max(0, int(text)))
        self._gap_var.set(str(self._gap))
        self._update_warning()
        self._sync_preview()

    @staticmethod
    def _parse_int(text: str):
        """空串→None（自动），正整数→int，非法→None。"""
        text = text.strip()
        return int(text) if text.isdigit() else None

    def _sync_pos_vars(self, i: int) -> None:
        """拖拽/拉伸后同步 x/y/宽 输入框与自动勾选。"""
        if i not in self._pos_vars:
            return
        auto_var, x_var, y_var, w_var = self._pos_vars[i]
        w = self._widgets[i]
        auto_var.set(w.get("x") is None and w.get("y") is None)
        x_var.set("" if w.get("x") is None else str(w["x"]))
        y_var.set("" if w.get("y") is None else str(w["y"]))
        w_var.set("" if w.get("w") is None else str(w["w"]))

    def _toggle_auto(self, i: int) -> None:
        auto_var, x_var, y_var, _ = self._pos_vars[i]
        if auto_var.get():
            self._widgets[i]["x"] = None
            self._widgets[i]["y"] = None
            x_var.set(""); y_var.set("")
        else:
            self._widgets[i]["x"] = self._parse_int(x_var.get()) or 4
            self._widgets[i]["y"] = self._parse_int(y_var.get()) or 2
        self._update_warning()
        self._sync_preview()

    def _apply_layout(self, i: int) -> None:
        """焦点离开/回车时把 x/y/宽 文本写回配置。"""
        _, x_var, y_var, w_var = self._pos_vars[i]
        self._widgets[i]["x"] = self._parse_int(x_var.get())
        self._widgets[i]["y"] = self._parse_int(y_var.get())
        self._widgets[i]["w"] = self._parse_int(w_var.get())
        self._update_warning()
        self._sync_preview()

    def _bump_scale(self, i: int, direction: int) -> None:
        val = round(float(self._scale_vars[i].get()) + direction * _SCALE_STEP, 1)
        val = min(_SCALE_MAX, max(_SCALE_MIN, val))
        self._scale_vars[i].set(f"{val:.1f}")
        self._set(i, "scale", val)

    def _auto_arrange(self) -> None:
        for w in self._widgets:
            w["x"], w["y"] = None, None
        self._build_cards()
        self._sync_preview()

    def _sync_preview(self) -> None:
        if self._preview and self._preview.is_running:
            source = self._preview.source
            if isinstance(source, DashboardSource):
                source.update_config(self._temp_dash_cfg())
                self._update_warning()

    def _toggle(self, i: int, var: ctk.BooleanVar) -> None:
        self._widgets[i]["enabled"] = bool(var.get())
        self._update_warning()
        self._sync_preview()

    def _set(self, i: int, key: str, value) -> None:
        self._widgets[i][key] = value
        self._update_warning()
        self._sync_preview()

    def _move(self, i: int, delta: int) -> None:
        j = i + delta
        if 0 <= j < len(self._widgets):
            self._widgets[i], self._widgets[j] = self._widgets[j], self._widgets[i]
            self._build_cards()
            self._sync_preview()

    def _pick_color(self, i: int) -> None:
        _, hexs = cc.askcolor(color=self._widgets[i].get("color", "#FFFFFF"))
        if hexs:
            self._widgets[i]["color"] = hexs.upper()
            self._build_cards()
            self._sync_preview()

    def _pick_background(self) -> None:
        _, hexs = cc.askcolor(color=self._bg)
        if hexs:
            self._bg = hexs.upper()
            self._bg_btn.configure(text=self._bg, fg_color=self._bg)
            self._sync_preview()

    def _pick_bg_image(self) -> None:
        path = fd.askopenfilename(
            title="选择背景图片",
            filetypes=[("图片", "*.png *.jpg *.jpeg *.bmp *.gif *.webp"),
                       ("所有文件", "*.*")])
        if path:
            self._bg_image = path
            self._img_lbl.configure(text=self._img_short())
            self._sync_preview()

    def _clear_bg_image(self) -> None:
        self._bg_image = None
        self._img_lbl.configure(text=self._img_short())
        self._sync_preview()

    def _update_warning(self) -> None:
        width, height = (self.cfg["video"]["target_width"],
                         self.cfg["video"]["target_height"])
        total = compute_layout_height(self._widgets, width, self._gap)
        if total > height:
            self._warn.configure(text=f"⚠ 自动排列组件总高 {total}px 超出画布 {height}px")
        else:
            self._warn.configure(text="")

    def apply(self) -> None:
        """校验刷新间隔并写回仪表盘配置。"""
        try:
            interval = float(self._refresh.get().strip())
        except ValueError:
            self._warn.configure(text="⚠ 刷新间隔必须是数字（秒）")
            return
        self._apply_gap()
        self.cfg["sources"]["dashboard"] = {
            "background": self._bg, "bg_image": self._bg_image,
            "refresh_interval_sec": max(0.2, interval),
            "gap": self._gap,
            "widgets": deepcopy(self._widgets)}
        self._on_apply("dashboard")

    def on_show(self) -> None:
        self._update_warning()
        self._preview.set_source(DashboardSource(
            (self.cfg["video"]["target_width"],
             self.cfg["video"]["target_height"]), self._temp_dash_cfg()))
        self._preview.start()
        # 重置温度列表（预览源是新建的，等待后台扫描）
        self._temp_rendered_keys = []
        if self._temp_hint:
            self._temp_hint.configure(text="等待扫描…", text_color="#FFC107")
        self._temp_poll_id = self.after(800, self._poll_temp)

    def on_hide(self) -> None:
        if self._temp_poll_id is not None:
            self.after_cancel(self._temp_poll_id)
            self._temp_poll_id = None
        self._preview.stop()
