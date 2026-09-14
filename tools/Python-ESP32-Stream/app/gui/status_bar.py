"""状态栏：开始/停止按钮、连接灯、目标、实时 FPS、帧计数。"""
from __future__ import annotations

import customtkinter as ctk

# 连接状态 -> (灯色, 文案)
_STATE = {
    "idle": ("#666666", "未启动"),
    "connecting": ("#FFC107", "连接中…"),
    "connected": ("#00E676", "已连接"),
    "disconnected": ("#FF5252", "已断开，重连中"),
}


class StatusBar(ctk.CTkFrame):
    """底部状态栏与启停控制。"""

    def __init__(self, master, on_toggle, **kwargs):
        super().__init__(master, **kwargs)
        self._on_toggle = on_toggle
        self.grid_columnconfigure(3, weight=1)
        self._btn = ctk.CTkButton(self, text="开始推流", width=110,
                                  command=self._click)
        self._btn.grid(row=0, column=0, padx=8, pady=6)
        self._lamp = ctk.CTkLabel(self, text="●", text_color="#666666",
                                  font=("Segoe UI", 16))
        self._lamp.grid(row=0, column=1, padx=(2, 4))
        self._state = ctk.CTkLabel(self, text="未启动", width=120, anchor="w")
        self._state.grid(row=0, column=2, sticky="w")
        self._info = ctk.CTkLabel(self, text="", anchor="e")
        self._info.grid(row=0, column=3, sticky="e", padx=10)
        self._streaming = False

    def _click(self) -> None:
        self._on_toggle()

    def set_streaming(self, on: bool, mode: str = "") -> None:
        """更新启停按钮文案。"""
        self._streaming = on
        self._btn.configure(text="停止推流" if on else "开始推流")

    def set_conn_state(self, state: str, host: str = "") -> None:
        """更新连接灯与状态文案。"""
        color, text = _STATE.get(state, _STATE["idle"])
        if host and state in ("connecting", "connected"):
            text = f"{text} {host}"
        if not self._streaming and state == "idle":
            text = "未启动"
        self._lamp.configure(text_color=color)
        self._state.configure(text=text)

    def set_stats(self, fps: float, frames: int) -> None:
        self._info.configure(text=f"FPS {fps:.1f}   帧 {frames}")
