"""帧消费线程：脏矩形差分、自适应阈值、RGB565 分包发送、心跳保活。"""
from __future__ import annotations

import logging
import queue
import socket
import threading
import time
from collections import deque

from PIL import Image

from . import imaging

log = logging.getLogger(__name__)

# 每包发送后短暂让步，给 ESP32 留渲染时间
_CHUNK_SLEEP = 0.005
# 状态事件最小上报间隔
_STATS_INTERVAL = 1.0


class ConnectionHolder:
    """跨线程共享当前 TCP 连接（pipeline 写，consumer 读）。"""

    def __init__(self):
        self.sock: socket.socket | None = None


class FrameConsumer:
    """单 session 的帧消费/发送循环。"""

    def __init__(self, settings: dict, frames_queue: queue.Queue,
                 holder: ConnectionHolder, event_queue: queue.Queue,
                 global_stop: threading.Event, internal_stop: threading.Event):
        self.s = settings
        self.q = frames_queue
        self.holder = holder
        self.events = event_queue
        self.global_stop = global_stop
        self.internal_stop = internal_stop
        self._prev: Image.Image | None = None
        self._force_full = False
        self._threshold = settings.get("min_dirty_rect_threshold", 1)
        self._times: deque = deque(maxlen=settings.get("fps_history_size", 3))
        self._frames_sent = 0
        self._last_heartbeat = 0.0
        self._last_stats = 0.0

    def reset_full_frame(self) -> None:
        """请求下一帧全量发送（热切换画面源后调用）。"""
        self._force_full = True

    def run(self) -> None:
        """consumer 主循环，socket 错误时退出由 pipeline 重连。"""
        log.info("消费线程启动")
        self._last_heartbeat = time.monotonic()
        last_frame_t = time.monotonic()
        while not self._should_stop():
            try:
                frame = self.q.get(timeout=0.5)
            except queue.Empty:
                if not self._heartbeat_or_break():
                    break
                continue
            now = time.monotonic()
            frame_interval = now - last_frame_t
            last_frame_t = now
            try:
                self._process(frame)
                self._update_threshold(frame_interval)
            except socket.error as e:
                log.warning("发送失败: %s，等待重连", e)
                break
            except Exception:
                log.exception("消费循环异常")
                break
            finally:
                self.q.task_done()
        self._emit_stats(force=True, connected=False)
        log.info("消费线程停止")

    def _heartbeat_or_break(self) -> bool:
        """空队列时发心跳；正在停止或 socket 已关闭返回 False 退出循环。"""
        if self._should_stop():
            return False
        try:
            self._maybe_heartbeat()
            return True
        except socket.error as e:
            log.warning("心跳发送失败: %s，等待重连", e)
            return False

    def _should_stop(self) -> bool:
        return self.internal_stop.is_set() or self.global_stop.is_set()

    def _process(self, frame: Image.Image) -> None:
        """处理一帧：差分→分包→发送。"""
        prev = None if self._force_full else self._prev
        self._force_full = False
        rect = imaging.find_dirty_rect(prev, frame, self._threshold)
        if rect is None:
            self._maybe_heartbeat()
        else:
            self._send_rect(frame, rect)
        self._prev = frame

    def _send_rect(self, frame: Image.Image, rect) -> None:
        """脏矩形按 max_chunk 切行转换并发送。"""
        x, y, w, h = rect
        max_data = self.s.get("max_chunk_data_size", 8192)
        for cx, cy, cw, ch in imaging.iter_chunk_rects(x, y, w, h, max_data):
            crop = frame.crop((cx, cy, cx + cw, cy + ch))
            data = imaging.image_to_rgb565_bytes(
                crop, self.s.get("gamma", 1.0), self.s.get("wb_scale", (1, 1, 1)))
            packet = imaging.pack_packet(cx, cy, cw, ch, data)
            if self.holder.sock is None:
                raise socket.error("连接已关闭")
            self.holder.sock.sendall(packet)
            self._frames_sent += 1
            time.sleep(_CHUNK_SLEEP)
        self._last_heartbeat = time.monotonic()

    def _maybe_heartbeat(self) -> None:
        """画面静止或队列空闲时周期发心跳，保持 NAT/固件侧连接。"""
        interval = self.s.get("heartbeat_interval_sec", 2.0)
        now = time.monotonic()
        if self.holder.sock is None or now - self._last_heartbeat < interval:
            return
        try:
            self.holder.sock.sendall(imaging.heartbeat_packet())
            self._last_heartbeat = now
        except socket.error:
            raise

    def _update_threshold(self, frame_interval: float) -> None:
        """按真实帧间隔（含等待）估算 FPS，自适应调整脏矩形阈值。"""
        self._times.append(frame_interval)
        if len(self._times) < self._times.maxlen:
            return
        avg = sum(self._times) / len(self._times)
        fps = 1.0 / avg if avg > 0 else 0.0
        target = self.s.get("target_fps", 24.0)
        hyst = target * self.s.get("fps_hysteresis_factor", 0.03)
        if fps < target - hyst:
            self._threshold = min(
                self.s.get("max_dirty_rect_threshold", 180),
                self._threshold + self.s.get("threshold_adjustment_step_up", 15))
        elif fps > target + hyst:
            self._threshold = max(
                self.s.get("min_dirty_rect_threshold", 1),
                self._threshold - self.s.get("threshold_adjustment_step_down", 5))
        self._emit_stats(fps=fps)

    def _emit_stats(self, fps: float = 0.0, force: bool = False,
                    connected: bool = True) -> None:
        """节流上报 FPS/帧计数给 GUI。"""
        now = time.monotonic()
        if not force and now - self._last_stats < _STATS_INTERVAL:
            return
        self._last_stats = now
        try:
            self.events.put_nowait(
                {"kind": "stats", "connected": connected,
                 "fps": round(fps, 1), "frames": self._frames_sent})
        except queue.Full:
            pass
