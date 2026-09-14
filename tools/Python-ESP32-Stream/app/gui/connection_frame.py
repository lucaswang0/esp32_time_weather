"""连接与画质设置区：IP/端口/广播/分辨率/FPS/伽马/白平衡，应用后整体重连。"""
from __future__ import annotations

import tkinter.messagebox as mb

import customtkinter as ctk


class ConnectionFrame(ctk.CTkFrame):
    """顶部连接参数表单；值直接读写 cfg 字典。"""

    def __init__(self, master, cfg: dict, on_apply, **kwargs):
        super().__init__(master, **kwargs)
        self.cfg = cfg
        self._on_apply = on_apply
        self._vars: dict[str, ctk.Variable] = {}
        self._build_row1()
        self._build_row2()
        self._btn = ctk.CTkButton(self, text="应用并重连", width=110,
                                  command=self.apply)
        self._btn.grid(row=0, column=8, rowspan=2, padx=10, pady=8)

    def _entry(self, label: str, key: str, value, width: int,
               row: int, col: int) -> None:
        ctk.CTkLabel(self, text=label).grid(row=row, column=col, padx=(8, 2),
                                            pady=4, sticky="e")
        var = ctk.StringVar(value=str(value))
        ctk.CTkEntry(self, textvariable=var, width=width).grid(
            row=row, column=col + 1, pady=4)
        self._vars[key] = var

    def _build_row1(self) -> None:
        c = self.cfg["connection"]
        v = self.cfg["video"]
        self._entry("ESP32 IP", "ip", c["esp32_host"], 110, 0, 0)
        self._entry("端口", "port", c["esp32_port"], 60, 0, 2)
        self._bcast = ctk.BooleanVar(value=c["use_broadcast"])
        ctk.CTkCheckBox(self, text="广播发现", variable=self._bcast).grid(
            row=0, column=4, padx=8)
        self._entry("宽", "width", v["target_width"], 50, 0, 5)
        self._entry("高", "height", v["target_height"], 50, 0, 6)

    def _build_row2(self) -> None:
        v = self.cfg["video"]
        self._entry("目标FPS", "fps", v["target_fps"], 50, 1, 0)
        self._entry("伽马", "gamma", v["gamma"], 50, 1, 2)
        wb = v["wb_scale"]
        self._entry("白平衡R", "wbr", wb[0], 45, 1, 4)
        self._entry("G", "wbg", wb[1], 45, 1, 5)
        self._entry("B", "wbb", wb[2], 45, 1, 6)

    def _read_int(self, key: str, section: str, field: str) -> int:
        return int(self._vars[key].get().strip())

    def _read_float(self, key: str, section: str, field: str) -> float:
        return float(self._vars[key].get().strip())

    def apply(self) -> bool:
        """解析并写回 cfg；非法输入弹框提示并放弃。"""
        try:
            c, v = self.cfg["connection"], self.cfg["video"]
            c["esp32_host"] = self._vars["ip"].get().strip()
            c["esp32_port"] = self._read_int("port", "connection", "esp32_port")
            c["use_broadcast"] = bool(self._bcast.get())
            v["target_width"] = self._read_int("width", "video", "target_width")
            v["target_height"] = self._read_int("height", "video", "target_height")
            v["target_fps"] = self._read_float("fps", "video", "target_fps")
            v["gamma"] = self._read_float("gamma", "video", "gamma")
            v["wb_scale"] = [
                self._read_float("wbr", "video", "wb"),
                self._read_float("wbg", "video", "wb"),
                self._read_float("wbb", "video", "wb")]
        except (ValueError, KeyError):
            mb.showerror("参数错误", "请检查数值字段是否填写正确（数字）")
            return False
        self._on_apply()
        return True
