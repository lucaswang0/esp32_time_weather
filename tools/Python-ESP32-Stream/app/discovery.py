"""UDP 广播发现：监听 ESP32 周期性广播 'ESP32:<ip>:<port>'，提供最新地址。"""
from __future__ import annotations

import logging
import socket
import threading
import time

log = logging.getLogger(__name__)


class BroadcastListener:
    """UDP 广播监听器，run() 在独立 daemon 线程中运行。"""

    def __init__(self, port: int):
        self._port = port
        self._lock = threading.Lock()
        self._last_ip: str | None = None
        self._last_ts = 0.0

    def _bind(self) -> socket.socket | None:
        """绑定 UDP 端口；端口占用等异常返回 None。"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("", self._port))
        except OSError as e:
            log.warning("广播端口 %d 绑定失败: %s", self._port, e)
            sock.close()
            return None
        sock.settimeout(1.0)
        return sock

    def run(self, stop_event: threading.Event) -> None:
        """接收广播包并记录最新 ESP32 IP。"""
        sock = self._bind()
        if sock is None:
            return
        log.info("广播发现监听已启动，端口 %d", self._port)
        while not stop_event.is_set():
            try:
                data, _ = sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                break
            self._handle_packet(data)
        sock.close()

    def _handle_packet(self, data: bytes) -> None:
        """解析 'ESP32:<ip>:<port>' 广播文本。"""
        text = data.decode("utf-8", errors="ignore").strip()
        parts = text.split(":")
        if len(parts) >= 3 and parts[0] == "ESP32":
            with self._lock:
                self._last_ip, self._last_ts = parts[1], time.time()
            log.info("收到 ESP32 广播: %s", parts[1])

    def fresh_ip(self, hold_time: float) -> str | None:
        """返回 hold_time 秒内有效的广播 IP，过期或没有则 None。"""
        with self._lock:
            if self._last_ip and time.time() - self._last_ts < hold_time:
                return self._last_ip
        return None
