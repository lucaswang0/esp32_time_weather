//go:build windows

package transport

import "net"

// setReuseAddr 在 Windows 上尽力开启 SO_REUSEADDR。
// 失败不致命 — 单实例运行无影响。
func setReuseAddr(conn *net.UDPConn) error {
	raw, err := conn.SyscallConn()
	if err != nil {
		return err
	}
	var inner error
	if err := raw.Control(func(fd uintptr) {
		// 调用 ws2_32 setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
		inner = windowsSetReuseAddr(fd)
	}); err != nil {
		return err
	}
	return inner
}
