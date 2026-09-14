"""推流管线：广播发现、主动连接 ESP32、生成/消费线程编排、源热切换。

线路协议与旧版完全一致（见 imaging.py 模块说明），ESP32 固件零改动。
"""
from __future__ import annotations

import logging
import queue
import socket
import threading
import time

from PIL import Image

from .consumer import ConnectionHolder, FrameConsumer
from .discovery import BroadcastListener
from .sources.base import FrameSource

log = logging.getLogger(__name__)

_SWAP_TIMEOUT = 5.0


class StreamPipeline:
    """单条推流管线：管理连接生命周期与帧源热切换。"""

    def __init__(self, settings: dict, source: FrameSource,
                 event_queue: queue.Queue, global_stop: threading.Event):
        self.s = settings
        self.events = event_queue
        self.global_stop = global_stop
        self._terminate = threading.Event()
        self.holder = ConnectionHolder()
        self._q = queue.Queue(maxsize=settings.get("frames_queue_max_size", 8))
        self._lock = threading.Lock()
        self._source = source
        self._pending: FrameSource | None = None
        self._switch_flag = threading.Event()
        self._switch_done = threading.Event()
        self._session_active = False
        self._consumer: FrameConsumer | None = None
        self._bcast: BroadcastListener | None = None
        self._manager_thread: threading.Thread | None = None

    def start(self) -> None:
        """启动广播监听与连接管理线程。"""
        if self.s.get("use_broadcast"):
            self._bcast = BroadcastListener(self.s.get("broadcast_port", 8889))
            threading.Thread(target=self._bcast.run, args=(self.global_stop,),
                             daemon=True, name="Bcast").start()
        self._manager_thread = threading.Thread(
            target=self._manager_loop, daemon=True, name="PipelineMgr")
        self._manager_thread.start()

    def stop(self) -> None:
        """终止管线并唤醒可能阻塞的连接。"""
        self._terminate.set()
        if self.holder.sock:
            try:
                self.holder.sock.close()
            except OSError:
                pass

    def join(self, timeout: float = 6.0) -> None:
        if self._manager_thread:
            self._manager_thread.join(timeout)

    def switch_source(self, new_source: FrameSource) -> bool:
        """热切换帧源：session 内由生成线程换源，session 外直接替换。"""
        self._switch_done.clear()
        with self._lock:
            if not self._session_active:
                self._source = new_source
                return True
            self._pending = new_source
            self._switch_flag.set()
        return self._switch_done.wait(_SWAP_TIMEOUT)

    # ---------- 连接管理 ----------

    def _resolve_host(self) -> str | None:
        """配置 IP 优先；广播 IP 在有效期内覆盖。"""
        host = self.s.get("esp32_host")
        if self._bcast:
            fresh = self._bcast.fresh_ip(self.s.get("broadcast_hold_time", 30))
            if fresh:
                log.info("使用广播 IP: %s", fresh)
                return fresh
        return host

    def _emit_conn(self, state: str, host: str = "") -> None:
        try:
            self.events.put_nowait({"kind": "conn", "state": state, "host": host})
        except queue.Full:
            pass

    def _connect(self, host: str) -> socket.socket | None:
        """建立 TCP 连接并设置 NODELAY。"""
        port = self.s["esp32_port"]
        try:
            sock = socket.create_connection(
                (host, port), timeout=self.s.get("socket_timeout", 1.0))
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return sock
        except OSError as e:
            # 关闭途中的连接竞态（socket 被 stop 关闭）不算异常
            if not self._closing():
                log.warning("连接 %s:%s 失败: %s", host, port, e)
            return None

    def _manager_loop(self) -> None:
        """连接-会话-重连主循环。"""
        reconnect = self.s.get("reconnect_interval_sec", 3.0)
        while not self._closing():
            host = self._resolve_host()
            if not host:
                log.error("无有效 ESP32 地址，%ss 后重试", reconnect)
                self._wait(reconnect)
                continue
            self._emit_conn("connecting", host)
            sock = self._connect(host)
            if sock is None:
                self._wait(reconnect)
                continue
            self._run_session(sock, host)
            if not self._closing():
                log.info("连接断开，%ss 后重连", reconnect)
                self._wait(reconnect)
        log.info("管线管理线程退出")

    def _closing(self) -> bool:
        return self.global_stop.is_set() or self._terminate.is_set()

    def _wait(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end and not self._closing():
            time.sleep(0.1)

    def _run_session(self, sock: socket.socket, host: str) -> None:
        """运行一次连接会话：起生成/消费线程并等待消费线程结束。"""
        self.holder.sock = sock
        self._emit_conn("connected", host)
        log.info("已连接 ESP32: %s", host)
        session_stop = threading.Event()
        gen = threading.Thread(target=self._gen_loop, args=(session_stop,),
                               daemon=True, name="FrameGen")
        self._consumer = FrameConsumer(
            self.s, self._q, self.holder, self.events,
            self.global_stop, session_stop)
        con = threading.Thread(target=self._consumer.run, daemon=True,
                               name="FrameCon")
        with self._lock:
            self._session_active = True
        gen.start()
        con.start()
        con.join()
        # 消费线程结束（断网/错误）→ 结束会话
        session_stop.set()
        gen.join(timeout=3)
        with self._lock:
            self._session_active = False
        self._consumer = None
        self.holder.sock = None
        try:
            sock.close()
        except OSError:
            pass
        self._emit_conn("disconnected", host)

    # ---------- 帧生成 ----------

    def _take_pending(self) -> FrameSource | None:
        """加锁取走待切换源并清标志。"""
        with self._lock:
            new = self._pending
            self._pending = None
            self._switch_flag.clear()
            return new

    def _drain_queue(self) -> None:
        while True:
            try:
                self._q.get_nowait()
                self._q.task_done()
            except queue.Empty:
                return

    def _swap_source(self, source: FrameSource) -> FrameSource:
        """关闭旧源、打开新源、清队列、通知 consumer 全量刷新。"""
        new = self._take_pending()
        if new is None:
            return source
        try:
            source.close()
            new.open()
        except Exception:
            log.exception("切换帧源失败，保留旧源")
            self._switch_done.set()
            return source
        self._drain_queue()
        if self._consumer:
            self._consumer.reset_full_frame()
        log.info("帧源已切换: %s", type(new).__name__)
        self._switch_done.set()
        return new

    def _gen_loop(self, session_stop: threading.Event) -> None:
        """帧生成线程；mss 等资源在本线程内 open/close。"""
        source = self._source
        try:
            source.open()
        except Exception:
            log.exception("帧源打开失败，结束会话")
            session_stop.set()
            return
        interval = self.s.get("generator_target_interval_sec", 0.03)
        low = self.s.get("generator_low_water_mark", 3)
        size = (self.s["target_width"], self.s["target_height"])
        try:
            source = self._generate(session_stop, source, interval, low, size)
        finally:
            try:
                source.close()
            except Exception:
                log.exception("关闭帧源异常")
        log.info("生成线程退出")

    def _stopped(self, session_stop: threading.Event) -> bool:
        return session_stop.is_set() or self._closing()

    def _generate(self, session_stop, source, interval: float,
                  low: int, size) -> FrameSource:
        """生成循环主体，返回当前（可能已切换的）源。"""
        while not self._stopped(session_stop):
            if self._switch_flag.is_set():
                source = self._swap_source(source)
                continue
            if self._q.qsize() >= low:
                session_stop.wait(0.01)
                continue
            loop_start = time.monotonic()
            canvas = Image.new("RGB", size, (0, 0, 0))
            if source.draw_frame(canvas):
                try:
                    self._q.put(canvas, timeout=0.1)
                except queue.Full:
                    pass
            else:
                session_stop.wait(0.1)
            elapsed = time.monotonic() - loop_start
            session_stop.wait(max(0.0, interval - elapsed))
        return source
