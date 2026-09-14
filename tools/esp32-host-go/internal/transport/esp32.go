package transport

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"esp32_host/internal/config"
	"esp32_host/internal/protocol"
)

// ESP32Client 单个 pipeline 与一台 ESP32 的 TCP 通讯客户端。
//
//   - 主动拨号，使用 SetNoDelay(true) 降低小包延迟
//   - 写操作受写锁保护（心跳与脏矩形共享同一条 TCP 连接）
//   - 不在本类实现重连/发现 → 调用方在更上层循环控制
type ESP32Client struct {
	cfg  config.PipelineConf
	disc *Discovery
	log  *slog.Logger

	mu        sync.Mutex
	conn      net.Conn
	addr      string
	connected atomic.Bool
}

// NewESP32Client 构造客户端（不立即连接）。
func NewESP32Client(cfg config.PipelineConf, disc *Discovery, logger *slog.Logger) (*ESP32Client, error) {
	if logger == nil {
		logger = slog.Default()
	}
	if cfg.ESP32.Port == 0 {
		return nil, fmt.Errorf("esp32 port not configured for pipeline %q", cfg.Name)
	}
	return &ESP32Client{
		cfg:  cfg,
		disc: disc,
		log:  logger.With("subsystem", "esp32", "pipeline", cfg.Name),
	}, nil
}

// Connect 选择目标 IP 后建立 TCP 连接。
//
// 选 IP 优先级：
//  1. config.esp32.host 非空 → 直接使用
//  2. config.esp32.use_broadcast=true 且 Discovery 在 ttl 内有有效广播 → 用广播 IP
//  3. 返回 error，由上层 sleep 后重试
func (c *ESP32Client) Connect(ctx context.Context) error {
	ip, port, err := c.resolveTarget()
	if err != nil {
		return err
	}
	target := net.JoinHostPort(ip, itoa(port))
	c.log.Info("dialing", "target", target, "timeout_sec", c.cfg.ESP32.SocketTimeoutSec)

	d := net.Dialer{Timeout: c.cfg.SocketTimeout()}
	conn, err := d.DialContext(ctx, "tcp", target)
	if err != nil {
		return fmt.Errorf("dial %s: %w", target, err)
	}
	if tc, ok := conn.(*net.TCPConn); ok {
		if err := tc.SetNoDelay(true); err != nil {
			c.log.Warn("set TCP_NODELAY failed", "err", err)
		}
	}

	c.mu.Lock()
	c.conn = conn
	c.addr = target
	c.mu.Unlock()
	c.connected.Store(true)
	c.log.Info("connected", "remote", target)
	return nil
}

// RemoteAddr 返回当前连接的远端地址，未连接返回 ""。
func (c *ESP32Client) RemoteAddr() string {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.addr
}

// IsConnected 是否有活跃连接。
func (c *ESP32Client) IsConnected() bool {
	return c.connected.Load()
}

// Close 关闭底层 TCP 连接。
func (c *ESP32Client) Close() {
	c.mu.Lock()
	conn := c.conn
	c.conn = nil
	c.addr = ""
	c.mu.Unlock()
	c.connected.Store(false)
	if conn != nil {
		if err := conn.Close(); err != nil {
			c.log.Debug("close conn", "err", err)
		}
	}
}

// SendHeartbeat 发送 12 字节心跳包。
func (c *ESP32Client) SendHeartbeat() error {
	return c.writeAll(protocol.HeartbeatPacket())
}

// SendRect 把单个矩形切分并按 maxChunkData 发送多个子包。
// rgb565 必须是已经过 gamma/wb_scale + RGB565 编码的字节流，长度 == w*h*2。
func (c *ESP32Client) SendRect(x, y, w, h uint16, rgb565 []byte) error {
	chunks := protocol.ChunkRect(int(x), int(y), int(w), int(h), MaxChunkData)
	if len(chunks) == 0 {
		return fmt.Errorf("SendRect: empty chunks for (%d,%d,%d,%d)", x, y, w, h)
	}
	if len(chunks) == 1 {
		pkt, err := protocol.PackRectPacket(x, y, w, h, rgb565)
		if err != nil {
			return err
		}
		return c.writeAll(pkt)
	}
	// 切分发送：每个子矩形取原 rect 中相应行的字节 (w * subH * 2)
	stride := int(w) * 2
	for _, r := range chunks {
		start := (r.Y - int(y)) * stride
		end := start + r.W*r.H*2
		if start < 0 || end > len(rgb565) {
			return fmt.Errorf("SendRect: chunk slice out of range")
		}
		pkt, err := protocol.PackRectPacket(uint16(r.X), uint16(r.Y), uint16(r.W), uint16(r.H), rgb565[start:end])
		if err != nil {
			return err
		}
		if err := c.writeAll(pkt); err != nil {
			return err
		}
		// 与 Python 版保持 5 ms 间隔，避免在 WiFi 上过冲
		time.Sleep(5 * time.Millisecond)
	}
	return nil
}

// resolveTarget 选择目标 IP:Port。
func (c *ESP32Client) resolveTarget() (string, int, error) {
	host := c.cfg.ESP32.Host
	port := c.cfg.ESP32.Port
	if host != "" {
		return host, port, nil
	}
	if !c.cfg.ESP32.UseBroadcast {
		return "", 0, fmt.Errorf("esp32.host is empty and use_broadcast=false")
	}
	if c.disc == nil {
		return "", 0, fmt.Errorf("esp32.host is empty and no discovery configured")
	}
	ip, pt, ok := c.disc.LatestWithin(c.cfg.BroadcastHold())
	if !ok {
		return "", 0, fmt.Errorf("no valid ESP32 broadcast within %s", c.cfg.BroadcastHold())
	}
	return ip, pt, nil
}

// writeAll 持锁写整个缓冲区。
//
// 失败时立即关闭底层 conn 并清空状态，避免后续调用继续使用一个已死的 socket
// （否则 connect flag=false 但 conn 仍持有旧 fd，下次 write 仍会立刻失败）。
func (c *ESP32Client) writeAll(buf []byte) error {
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()
	if conn == nil {
		return ErrNotConnected
	}
	if _, err := conn.Write(buf); err != nil {
		// 写失败：连接已不可用，主动关闭并清空本地状态。
		// connectionLoop 会观察到 connected=false → 触发重连。
		c.Close()
		return fmt.Errorf("write %d bytes: %w", len(buf), err)
	}
	return nil
}

// MaxChunkData 与 ESP32 StreamingPlayerPage.h::MAX_CHUNK_SIZE 保持一致。
const MaxChunkData = 8192

// itoa 非负整数快速转字符串，避免 fmt.Sprintf 在热路径分配。
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [20]byte
	pos := len(b)
	for n > 0 {
		pos--
		b[pos] = byte('0' + n%10)
		n /= 10
	}
	return string(b[pos:])
}
