//go:build windows

package transport

import "syscall"

// windowsSetReuseAddr 调用 ws2_32!setsockopt 设置 SO_REUSEADDR = 1。
//
//	M1 阶段保持最小实现：失败即返回错误，由调用方降级为忽略。
//	如果后续真有多实例并存需求，再补完整 syscall.Setsockopt 调用。
func windowsSetReuseAddr(fd uintptr) error {
	// SOL_SOCKET = 0xFFFF (Windows) / 1 (大多数 Unix)；这里取 Windows 值。
	const (
		solSocket   = 0xFFFF
		soReuseAddr = 0x0004
	)
	// syscall.SetsockoptInt 在 Windows 上对 UDP 正常工作
	return syscall.SetsockoptInt(syscall.Handle(fd), solSocket, soReuseAddr, 1)
}
