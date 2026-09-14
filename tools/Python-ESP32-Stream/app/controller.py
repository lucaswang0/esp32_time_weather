"""推流控制器：GUI 的唯一门面，负责启停、热切换、配置变更后的整体重启。"""
from __future__ import annotations

import logging
import queue
import threading

from .app_config import build_pipeline_settings
from .pipeline import StreamPipeline
from .sources.base import FrameSource
from .sources.desktop_source import DesktopSource
from .sources.dashboard_source import DashboardSource
from .sources.window_source import WindowSource

log = logging.getLogger(__name__)


def build_source(mode: str, cfg: dict) -> FrameSource:
    """根据模式与配置构造帧源（未 open，由生成线程打开）。"""
    res = (cfg["video"]["target_width"], cfg["video"]["target_height"])
    if mode == "desktop":
        sc = cfg["sources"]["desktop"]
        return DesktopSource(res, sc.get("monitor", 0),
                             sc.get("region"), sc.get("crop_alignment", "center"))
    if mode == "window":
        sc = cfg["sources"]["window"]
        return WindowSource(res, sc.get("window_title", ""),
                            sc.get("crop_alignment", "center"))
    if mode == "dashboard":
        return DashboardSource(res, cfg["sources"]["dashboard"])
    raise ValueError(f"未知画面源模式: {mode}")


class StreamController:
    """线程安全门面；所有方法可在 GUI 主线程调用，工作线程不碰 tk。"""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.event_queue: queue.Queue = queue.Queue(maxsize=200)
        self.global_stop = threading.Event()
        self._lock = threading.Lock()
        self._pipeline: StreamPipeline | None = None
        self._source: FrameSource | None = None
        self._mode = cfg.get("active_source", "window")

    @property
    def mode(self) -> str:
        return self._mode

    def is_streaming(self) -> bool:
        with self._lock:
            return self._pipeline is not None

    def start(self) -> bool:
        """按当前配置启动推流；已在运行则忽略。"""
        with self._lock:
            if self._pipeline is not None:
                return False
            self._source = build_source(self._mode, self.cfg)
            settings = build_pipeline_settings(self.cfg)
            self._pipeline = StreamPipeline(
                settings, self._source, self.event_queue, self.global_stop)
            self._pipeline.start()
        log.info("推流已启动（模式: %s）", self._mode)
        self._emit({"kind": "streaming", "on": True, "mode": self._mode})
        return True

    def stop(self) -> None:
        """停止推流并释放帧源资源。"""
        with self._lock:
            pipeline, source = self._pipeline, self._source
            self._pipeline, self._source = None, None
        if pipeline:
            pipeline.stop()
            pipeline.join()
        if source:
            self._safe_close(source)
        log.info("推流已停止")
        self._emit({"kind": "streaming", "on": False})

    def switch_source(self, mode: str) -> bool:
        """运行时热切换画面源；未推流时只记录模式。"""
        if mode not in ("desktop", "window", "dashboard"):
            return False
        new_source = build_source(mode, self.cfg)
        with self._lock:
            pipeline = self._pipeline
            self._mode, self._source = mode, new_source
        if pipeline is None:
            # 未开播：源尚未 open，无需关闭；start 时直接使用
            return True
        ok = pipeline.switch_source(new_source)
        if not ok:
            log.warning("热切换等待超时，已提交切换请求")
        # 旧源由生成线程在换源时自行 close，此处不重复释放
        log.info("画面源切换为: %s", mode)
        self._emit({"kind": "streaming", "on": True, "mode": mode})
        return True

    def restart(self) -> None:
        """连接/视频参数变更后整体重启（不断程序，TCP 重连）。"""
        running = self.is_streaming()
        if running:
            self.stop()
        self.start()

    def shutdown(self) -> None:
        """应用退出：停推流并通知所有线程结束。"""
        self.stop()
        self.global_stop.set()

    def _safe_close(self, source: FrameSource) -> None:
        try:
            source.close()
        except Exception:
            log.exception("关闭帧源异常")

    def _emit(self, event: dict) -> None:
        try:
            self.event_queue.put_nowait(event)
        except queue.Full:
            pass
