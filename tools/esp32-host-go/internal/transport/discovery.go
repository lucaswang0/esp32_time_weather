// Package transport 实现与 ESP32 设备的网络通讯。
//
//   - Discovery: 后台 UDP 监听 8889，解析 ESP32 广播的 "ESP32:<ip>:<port>"
//   - ESP32Client: 主动 TCP 连接 ESP32:8888，启用 TCP_NODELAY，
//     通过 SendHeartbeat / SendRect 发送协议包。
package transport

import (
	"errors"
	"fmt"
	"log/slog"
	"net"
	"sync"
	"time"
)

// ErrNotConnected 当前没有可用的 TCP 连接。
var ErrNotConnected = errors.New("transport: not connected")

// Discovery 在后台监听 ESP32 周期性 UDP 广播，并把最近一次广播的
// (ip, port, time) 暴露给所有 pipeline 使用。
//
// 广播报文格式（来自 ESP32 StreamingPlayerPage.cpp sendBroadcast）：
//
//	snprintf(msg, ..., "ESP32:%s:%d", localIP, SERVER_PORT);
//
// 默认端口 8889，对应配置 global.udp_broadcast_port。
type Discovery struct {
	port  int
	conn  *net.UDPConn
	log   *slog.Logger
	mu    sync.RWMutex
	ip    string
	pt    int
	at    time.Time
	stop  chan struct{}
	once  sync.Once
	stopped bool
}

// NewDiscovery 构造 Discovery，但**不**自动启动监听。
// 调用 Start() 开始接收广播。
func NewDiscovery(port int, logger *slog.Logger) *Discovery {
	if logger == nil {
		logger = slog.Default()
	}
	return &Discovery{
		port: port,
		log:  logger.With("subsystem", "discovery"),
		stop: make(chan struct{}),
	}
}

// Start 启动后台监听 goroutine。重复调用安全。
func (d *Discovery) Start() error {
	addr := &net.UDPAddr{IP: net.IPv4zero, Port: d.port}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return fmt.Errorf("discovery: listen udp :%d: %w", d.port, err)
	}
	// Linux 上 net.ListenUDP 默认开启 SO_REUSEADDR；Windows 上需要显式设置。
	// 这里容忍失败，单实例运行通常不受影响。
	if err := setReuseAddr(conn); err != nil {
		d.log.Debug("set SO_REUSEADDR skipped", "err", err)
	}
	d.conn = conn
	d.log.Info("discovery listening", "port", d.port)

	go d.loop()
	return nil
}

// Latest 返回最近一次广播的 IP/Port 与时间。
// 第二个返回值为 false 表示从未收到过广播。
func (d *Discovery) Latest() (string, int, time.Time, bool) {
	d.mu.RLock()
	defer d.mu.RUnlock()
	return d.ip, d.pt, d.at, d.ip != ""
}

// LatestWithin 返回在 ttl 之内仍然有效的广播。
// 若无广播或已过期返回 false。
func (d *Discovery) LatestWithin(ttl time.Duration) (string, int, bool) {
	ip, pt, at, ok := d.Latest()
	if !ok {
		return "", 0, false
	}
	if time.Since(at) > ttl {
		return "", 0, false
	}
	return ip, pt, true
}

// Close 停止监听并关闭 UDP socket。
func (d *Discovery) Close() {
	d.once.Do(func() {
		close(d.stop)
		d.mu.Lock()
		d.stopped = true
		if d.conn != nil {
			_ = d.conn.Close()
		}
		d.mu.Unlock()
	})
}

func (d *Discovery) loop() {
	buf := make([]byte, 1024)
	for {
		select {
		case <-d.stop:
			return
		default:
		}
		_ = d.conn.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
		n, addr, err := d.conn.ReadFromUDP(buf)
		if err != nil {
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				continue
			}
			d.mu.RLock()
			stopped := d.stopped
			d.mu.RUnlock()
			if stopped {
				return
			}
			d.log.Warn("read udp error", "err", err)
			time.Sleep(200 * time.Millisecond)
			continue
		}
		text := string(buf[:n])
		ip, port, ok := parseBroadcast(text)
		if !ok {
			d.log.Debug("ignored non-ESP32 broadcast", "from", addr, "text", text)
			continue
		}
		d.mu.Lock()
		d.ip = ip
		d.pt = port
		d.at = time.Now()
		d.mu.Unlock()
		d.log.Info("ESP32 broadcast received", "ip", ip, "port", port, "from", addr)
	}
}

// parseBroadcast 解析 "ESP32:<ip>:<port>" 格式。
// 与 Python _author_broadcast 中的 split 行为保持一致。
func parseBroadcast(s string) (string, int, bool) {
	// 期望以 "ESP32:" 开头；按 ":" 切，至少 3 段。
	const prefix = "ESP32:"
	if len(s) < len(prefix) || s[:len(prefix)] != prefix {
		return "", 0, false
	}
	rest := s[len(prefix):]
	// 找最后一个 ":" 分割 port，前面整体作为 ip
	idx := -1
	for i := len(rest) - 1; i >= 0; i-- {
		if rest[i] == ':' {
			idx = i
			break
		}
	}
	if idx < 0 {
		return "", 0, false
	}
	ip := rest[:idx]
	portStr := rest[idx+1:]
	port, err := atoi(portStr)
	if err != nil || ip == "" {
		return "", 0, false
	}
	return ip, port, true
}

func atoi(s string) (int, error) {
	n := 0
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c < '0' || c > '9' {
			return 0, fmt.Errorf("not a digit: %q", string(c))
		}
		n = n*10 + int(c-'0')
	}
	return n, nil
}
