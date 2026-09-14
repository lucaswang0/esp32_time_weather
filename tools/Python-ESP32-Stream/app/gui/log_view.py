"""日志面板：从日志队列拉取并追加到只读文本框，自动裁剪行数。"""
from __future__ import annotations

import queue

import customtkinter as ctk

_MAX_LINES = 500
_BATCH = 20


class LogView(ctk.CTkFrame):
    """底部可折叠日志区域。"""

    def __init__(self, master, log_queue: queue.Queue, **kwargs):
        super().__init__(master, **kwargs)
        self._q = log_queue
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)
        self._head = ctk.CTkButton(self, text="日志 ▸", width=90,
                                   command=self.toggle)
        self._head.grid(row=0, column=0, sticky="w", padx=6, pady=(4, 0))
        self._box = ctk.CTkTextbox(self, height=130, font=("Consolas", 11))
        self._visible = False

    def toggle(self) -> None:
        """展开/收起日志区。"""
        self._visible = not self._visible
        if self._visible:
            self._box.grid(row=1, column=0, sticky="nsew", padx=6, pady=6)
            self._head.configure(text="日志 ▾")
        else:
            self._box.grid_forget()
            self._head.configure(text="日志 ▸")

    def poll(self) -> None:
        """主线程周期调用：批量取日志追加。"""
        for _ in range(_BATCH):
            try:
                line = self._q.get_nowait()
            except queue.Empty:
                break
            self._append(line)

    def _append(self, line: str) -> None:
        self._box.configure(state="normal")
        self._box.insert("end", line + "\n")
        # 超出上限从头裁剪
        count = int(self._box.index("end-1c").split(".")[0])
        if count > _MAX_LINES:
            self._box.delete("1.0", f"{count - _MAX_LINES}.0")
        self._box.configure(state="disabled")
        self._box.see("end")
