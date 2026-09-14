"""应用入口：DPI 感知、日志/配置初始化、GUI 与托盘装配、全局异常兜底。"""
from __future__ import annotations

import ctypes
import logging
import queue
import sys
import threading
import tkinter.messagebox as mb

# 注意：本文件既是包模块（python -m app.main）又是 PyInstaller 入口脚本，
# 入口脚本无包上下文，故此处必须使用绝对导入。
from app.app_config import load_config, save_config, validate
from app.controller import StreamController
from app.gui.main_window import MainWindow
from app.logging_setup import setup_logging
from app.tray import TrayController

log = logging.getLogger(__name__)


def enable_dpi_awareness() -> None:
    """PerMonitorV2 DPI 感知，保证 mss 抓取坐标与物理像素一致。"""
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass


def install_excepthooks(window_getter) -> None:
    """主线程/工作线程未捕获异常 → 日志 + 弹框（windowed 模式可见）。"""
    def show(exc):
        logging.getLogger(__name__).exception("未捕获异常: %s", exc)
        try:
            win = window_getter()
            if win:
                mb.showerror("程序异常", f"发生错误：{exc}\n详情见 logs/app.log")
        except Exception:
            pass

    def hook(exc_type, exc, tb):
        logging.getLogger(__name__).error("未捕获异常", exc_info=(exc_type, exc, tb))
        show(exc)

    def thread_hook(args):
        logging.getLogger(__name__).error("线程异常: %s", args.exc_value)
        show(args.exc_value)

    sys.excepthook = hook
    threading.excepthook = thread_hook


class Application:
    """组装控制器/主窗口/托盘并在主线程泵托盘命令。"""

    def __init__(self, log_queue: queue.Queue):
        self._log_queue = log_queue
        self.cfg = load_config()
        for warn in validate(self.cfg):
            log.warning("配置修正: %s", warn)
        save_config(self.cfg)
        self.controller = StreamController(self.cfg)
        self.command_queue: queue.Queue = queue.Queue()
        self.window: MainWindow | None = None
        self.tray: TrayController | None = None
        self._tray_hint_shown = False
        self._quitting = False

    def run(self) -> None:
        self.window = MainWindow(self.cfg, self.controller, self._log_queue,
                                 self._on_close_request)
        self.tray = TrayController(
            self.command_queue, lambda: self.window.conn_state,
            lambda: self.controller.mode, self.controller.is_streaming)
        self.window.set_state_listener(self._on_state)
        self.tray.start()
        if self.cfg["ui"].get("start_minimized"):
            self.window.hide_window()
        self._pump_tray_commands()
        self.window.mainloop()

    def _on_state(self, conn_state: str, streaming: bool, fps: float) -> None:
        if self.tray:
            self.tray.update(conn_state, streaming, fps)

    def _on_close_request(self) -> None:
        """点 X：配置为驻留托盘则隐藏，否则退出。"""
        if self.cfg["ui"].get("close_to_tray", True):
            self.window.hide_window()
            if not self._tray_hint_shown:
                self.tray.notify_hidden()
                self._tray_hint_shown = True
        else:
            self.quit_app()

    def _pump_tray_commands(self) -> None:
        """主线程消费托盘菜单命令。"""
        try:
            while True:
                cmd = self.command_queue.get_nowait()
                self._handle_tray_command(cmd)
        except queue.Empty:
            pass
        if not self._quitting:
            self.window.after(200, self._pump_tray_commands)

    def _handle_tray_command(self, cmd: str) -> None:
        if cmd == "show":
            self.window.show_window()
        elif cmd == "quit":
            self.quit_app()
        elif cmd == "toggle":
            self.window.tray_toggle_streaming()
        elif cmd.startswith("switch:"):
            self.window.select_mode(cmd.split(":", 1)[1])

    def quit_app(self) -> None:
        """唯一的真正退出路径：停推流、撤托盘、关预览、销毁窗口。"""
        if self._quitting:
            return
        self._quitting = True
        log.info("应用退出中…")
        try:
            self.window.destroy_previews()
            self.controller.shutdown()
            if self.tray:
                self.tray.stop()
        finally:
            self.window.after(50, self.window.destroy)


def main() -> None:
    enable_dpi_awareness()
    log_queue = setup_logging()
    log.info("ESP32 Stream 启动")
    app = Application(log_queue)
    install_excepthooks(lambda: app.window)
    app.run()


if __name__ == "__main__":
    main()
