// Package protocol 实现与 ESP32 固件 (StreamingPlayerPage.cpp) 字节级兼容的
// 串流协议：12 字节大端头 + RGB565 little-endian 像素 + 心跳包。
package protocol

import (
	"encoding/binary"
	"errors"
	"fmt"
)

// HeaderSize 单包头的字节数。
const HeaderSize = 12

// HeartbeatX / HeartbeatY / HeartbeatDataLen 是 ESP32 端用于识别心跳的固定值
// (StreamingPlayerPage.cpp:  X==0xFFFF && Y==0xFFFF && DataLen==0)。
const (
	HeartbeatX      uint16 = 0xFFFF
	HeartbeatY      uint16 = 0xFFFF
	HeartbeatDataLen uint32 = 0
)

// Header 表示一帧的描述信息：位置、尺寸与像素字节长度。
type Header struct {
	X       uint16
	Y       uint16
	W       uint16
	H       uint16
	DataLen uint32
}

// IsHeartbeat 判断当前头是否为心跳包。
func (h Header) IsHeartbeat() bool {
	return h.X == HeartbeatX && h.Y == HeartbeatY && h.DataLen == HeartbeatDataLen
}

// PackHeader 把头编码为 12 字节 (大端)，与 Python
//
//	struct.pack('!HHHH I', x, y, w, h, data_len)
//
// 等价。
func PackHeader(h Header) []byte {
	buf := make([]byte, HeaderSize)
	binary.BigEndian.PutUint16(buf[0:2], h.X)
	binary.BigEndian.PutUint16(buf[2:4], h.Y)
	binary.BigEndian.PutUint16(buf[4:6], h.W)
	binary.BigEndian.PutUint16(buf[6:8], h.H)
	binary.BigEndian.PutUint32(buf[8:12], h.DataLen)
	return buf
}

// UnpackHeader 解析 12 字节大端头。
// 返回的 Header 按值传递，避免内部 buffer 逃逸。
func UnpackHeader(buf []byte) (Header, error) {
	if len(buf) < HeaderSize {
		return Header{}, fmt.Errorf("protocol: header buffer too short: %d < %d", len(buf), HeaderSize)
	}
	return Header{
		X:       binary.BigEndian.Uint16(buf[0:2]),
		Y:       binary.BigEndian.Uint16(buf[2:4]),
		W:       binary.BigEndian.Uint16(buf[4:6]),
		H:       binary.BigEndian.Uint16(buf[6:8]),
		DataLen: binary.BigEndian.Uint32(buf[8:12]),
	}, nil
}

// HeartbeatPacket 返回 12 字节心跳包（无 body）。
func HeartbeatPacket() []byte {
	return PackHeader(Header{
		X:       HeartbeatX,
		Y:       HeartbeatY,
		W:       0,
		H:       0,
		DataLen: HeartbeatDataLen,
	})
}

// ErrInvalidRect 非法的脏矩形。
var ErrInvalidRect = errors.New("protocol: invalid rect")

// PackRectPacket 把脏矩形与已编码好的 RGB565 像素打包为 1 个网络包。
// 编码好的 RGB565 字节数应 == W*H*2；若不一致会返回错误，避免静默损坏。
func PackRectPacket(x, y, w, h uint16, rgb565 []byte) ([]byte, error) {
	if w == 0 || h == 0 {
		return nil, fmt.Errorf("%w: w=%d h=%d", ErrInvalidRect, w, h)
	}
	expected := int(w) * int(h) * 2
	if len(rgb565) != expected {
		return nil, fmt.Errorf("protocol: rgb565 length %d != w*h*2 = %d", len(rgb565), expected)
	}
	hdr := PackHeader(Header{X: x, Y: y, W: w, H: h, DataLen: uint32(expected)})
	out := make([]byte, 0, HeaderSize+expected)
	out = append(out, hdr...)
	out = append(out, rgb565...)
	return out, nil
}
