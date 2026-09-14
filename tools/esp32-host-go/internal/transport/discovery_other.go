//go:build !windows

package transport

import "net"

// setReuseAddr 在非 Windows 平台上：
//   - Linux: net.ListenUDP 已默认设置 SO_REUSEADDR，no-op 即可
//   - macOS: 同样 no-op，单实例场景足够
func setReuseAddr(conn *net.UDPConn) error {
	_ = conn
	return nil
}
