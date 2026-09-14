"""主窗口：连接设置、三种画面源 Tab、状态栏、日志、事件队列泵。"""
from __future__ import annotations

import logging

import customtkinter as ctk

from ..app_config import save_config
from ..controller import StreamController
from .connection_frame import ConnectionFrame
from .dashboard_panel import DashboardPanel
from .desktop_panel import DesktopPanel
from .log_view import LogView
from .status_bar import StatusBar
from .window_panel import WindowPanel

log = logging.getLogger(__name__)

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


class MainWindow(ctk.CTk):
    """应用主窗口；工作线程事件全部经 after 泵在主线程消费。"""

    def __init__(self, cfg: dict, controller: StreamController,
                 log_queue, on_close_request):
        super().__init__()
        self.cfg = cfg
        self.controller = controller
        self._on_close_request = on_close_request
        self._state_listener = None
        self._conn_state = "idle"
        self._last_fps = 0.0
        self.title("ESP32 Stream — 屏幕推流")
        self.geometry("960x700")
        self.minsize(820, 600)
        self.protocol("WM_DELETE_WINDOW", self._handle_close)
        self._build_layout(cfg, log_queue)
        self._pump_events()
        self._pump_logs()

    def _build_layout(self, cfg, log_queue) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)
        self._conn_frame = ConnectionFrame(self, cfg, self._apply_connection)
        self._conn_frame.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 4))
        self._tabs = ctk.CTkTabview(self, command=self._tab_changed)
        self._tabs.grid(row=1, column=0, sticky="nsew", padx=8)
        names = {"desktop": "桌面", "window": "指定窗口", "dashboard": "仪表盘"}
        for text in names.values():
            self._tabs.add(text)
        self._panels = {
            "desktop": DesktopPanel(self._tabs.tab(names["desktop"]),
                                    cfg, self._apply_mode),
            "window": WindowPanel(self._tabs.tab(names["window"]),
                                  cfg, self._apply_mode),
            "dashboard": DashboardPanel(self._tabs.tab(names["dashboard"]),
                                        cfg, self._apply_mode),
        }
        for key, panel in self._panels.items():
            panel.pack(fill="both", expand=True)
        active = cfg.get("active_source", "window")
        self._tabs.set(names[active])
        self._panels[active].on_show()
        self._status = StatusBar(self, self._toggle_streaming)
        self._status.grid(row=2, column=0, sticky="ew", padx=8, pady=4)
        self._logs = LogView(self, log_queue)
        self._logs.grid(row=3, column=0, sticky="ew", padx=8, pady=(0, 8))

    @property
    def conn_state(self) -> str:
        return self._conn_state

    def set_state_listener(self, fn) -> None:
        """注册状态回调（供托盘更新图标/tooltip）。"""
        self._state_listener = fn

    # ---------- 业务动作 ----------

    def _toggle_streaming(self) -> None:
        if self.controller.is_streaming():
            self.controller.stop()
        else:
            self.cfg["active_source"] = self._current_mode()
            save_config(self.cfg)
            self.controller.start()

    def _apply_connection(self) -> None:
        save_config(self.cfg)
        log.info("连接/视频参数已保存，推流重启")
        self.controller.restart()

    def _apply_mode(self, mode: str) -> None:
        """面板的"应用并切换"：保存配置并热切换。"""
        self.cfg["active_source"] = mode
        save_config(self.cfg)
        self.controller.switch_source(mode)

    def select_mode(self, mode: str) -> None:
        """托盘菜单触发：切 Tab 并执行模式应用。"""
        names = {"desktop": "桌面", "window": "指定窗口", "dashboard": "仪表盘"}
        if mode in names:
            self._tabs.set(names[mode])
            self._tab_changed()
            self._apply_mode(mode)

    def tray_toggle_streaming(self) -> None:
        self._toggle_streaming()

    def _current_mode(self) -> str:
        current = self._tabs.get()
        return {"桌面": "desktop", "指定窗口": "window",
                "仪表盘": "dashboard"}.get(current, "window")

    def _tab_changed(self, *_args) -> None:
        active = self._current_mode()
        for key, panel in self._panels.items():
            if key == active:
                panel.on_show()
            else:
                panel.on_hide()

    # ---------- 窗口显隐/退出 ----------

    def _handle_close(self) -> None:
        self._on_close_request()

    def show_window(self) -> None:
        self.deiconify()
        self.lift()
        self.focus_force()

    def hide_window(self) -> None:
        self.withdraw()

    # ---------- 事件泵 ----------

    def _pump_events(self) -> None:
        """拉取工作线程事件更新状态栏（每 200ms）。"""
        for _ in range(30):
            try:
                event = self.controller.event_queue.get_nowait()
            except Exception:
                break
            self._handle_event(event)
        self.after(200, self._pump_events)

    def _pump_logs(self) -> None:
        self._logs.poll()
        self.after(250, self._pump_logs)

    def _handle_event(self, event: dict) -> None:
        kind = event.get("kind")
        if kind == "conn":
            self._conn_state = event["state"]
            self._status.set_conn_state(event["state"], event.get("host", ""))
        elif kind == "stats":
            if event.get("connected"):
                self._last_fps = event["fps"]
                self._status.set_stats(event["fps"], event["frames"])
        elif kind == "streaming":
            self._status.set_streaming(event["on"], event.get("mode", ""))
            if not event["on"]:
                self._conn_state = "idle"
                self._status.set_conn_state("idle")
        if self._state_listener:
            self._state_listener(self._conn_state,
                                 self.controller.is_streaming(), self._last_fps)

    def destroy_previews(self) -> None:
        """退出前关闭全部预览源。"""
        for panel in self._panels.values():
            panel.on_hide()
