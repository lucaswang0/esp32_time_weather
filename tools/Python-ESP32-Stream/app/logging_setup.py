"""日志初始化：文件轮转 + 队列供 GUI 线程安全消费；windowed 模式不依赖 stdout。"""
import logging
import logging.handlers
import queue

from .paths import LOG_PATH

LOG_FORMAT = "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
LOG_DATEFMT = "%H:%M:%S"


def setup_logging() -> queue.Queue:
    """配置 root logger，返回日志队列；GUI 用 get_nowait 轮询渲染。"""
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    log_queue: queue.Queue = queue.Queue(maxsize=500)

    file_handler = logging.handlers.RotatingFileHandler(
        LOG_PATH, maxBytes=512 * 1024, backupCount=2, encoding="utf-8")
    file_handler.setFormatter(logging.Formatter(LOG_FORMAT))

    queue_handler = _QueueHandler(log_queue)
    queue_handler.setFormatter(logging.Formatter(LOG_FORMAT, LOG_DATEFMT))

    root = logging.getLogger()
    root.setLevel(logging.INFO)
    root.handlers.clear()
    root.addHandler(file_handler)
    root.addHandler(queue_handler)
    return log_queue


class _QueueHandler(logging.Handler):
    """把日志记录放进有界队列，满了丢弃最旧记录，绝不阻塞工作线程。"""

    def __init__(self, log_queue: queue.Queue):
        super().__init__()
        self._queue = log_queue

    def emit(self, record: logging.LogRecord) -> None:
        try:
            self._queue.put_nowait(self.format(record))
        except queue.Full:
            try:
                self._queue.get_nowait()
                self._queue.put_nowait(self.format(record))
            except Exception:
                pass
