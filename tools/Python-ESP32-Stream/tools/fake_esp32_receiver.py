"""假 ESP32 接收端：验证 PC 推流协议（不打包进 exe）。

用法：python tools/fake_esp32_receiver.py [port]
校验：12 字节包头、data_len 与实际字节数、心跳包内容；统计帧数并周期打印。
"""
import socket
import struct
import sys
import time

HEADER = struct.Struct("!HHHH I")


def recv_exact(conn: socket.socket, size: int) -> bytes:
    """循环读取直到收满 size 字节，连接断开抛 RuntimeError。"""
    chunks = []
    remain = size
    while remain > 0:
        data = conn.recv(remain)
        if not data:
            raise RuntimeError("连接断开")
        chunks.append(data)
        remain -= len(data)
    return b"".join(chunks)


def serve_once(port: int) -> None:
    """接受一次连接并持续解析直到断开。"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    print(f"[fake-esp32] 监听 0.0.0.0:{port}，等待 PC 连接...")
    conn, addr = srv.accept()
    print(f"[fake-esp32] 已连接: {addr}")
    packets = 0
    heartbeats = 0
    pixels = 0
    last_report = time.monotonic()
    try:
        while True:
            x, y, w, h, dlen = HEADER.unpack(recv_exact(conn, HEADER.size))
            payload = recv_exact(conn, dlen) if dlen else b""
            assert len(payload) == dlen, "data_len 与实际数据不符"
            packets += 1
            if x == 0xFFFF and y == 0xFFFF and dlen == 0:
                heartbeats += 1
            else:
                pixels += w * h
            now = time.monotonic()
            if now - last_report >= 2.0:
                print(f"[统计] 包={packets} 心跳={heartbeats} 像素={pixels}")
                last_report = now
    except (RuntimeError, ConnectionError) as e:
        print(f"[fake-esp32] 结束: {e}，共收 {packets} 包/{heartbeats} 心跳")
    finally:
        conn.close()
        srv.close()


if __name__ == "__main__":
    listen_port = int(sys.argv[1]) if len(sys.argv) > 1 else 8888
    serve_once(listen_port)
