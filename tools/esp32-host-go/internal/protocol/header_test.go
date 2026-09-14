package protocol

import (
	"bytes"
	"encoding/binary"
	"testing"
)

// TestPackHeader_MatchesPython 验证 PackHeader 输出与 Python
//
//	struct.pack('!HHHH I', x, y, w, h, data_len)
//
// 完全一致（! = network byte order = big-endian）。
func TestPackHeader_MatchesPython(t *testing.T) {
	x, y, w, h, dl := uint16(10), uint16(20), uint16(30), uint16(40), uint32(1234)
	got := PackHeader(Header{X: x, Y: y, W: w, H: h, DataLen: dl})

	// 期望字节直接由 big-endian 编码得到（等价于 Python struct.pack('!HHHH I', ...)）
	want := make([]byte, HeaderSize)
	binary.BigEndian.PutUint16(want[0:2], x)
	binary.BigEndian.PutUint16(want[2:4], y)
	binary.BigEndian.PutUint16(want[4:6], w)
	binary.BigEndian.PutUint16(want[6:8], h)
	binary.BigEndian.PutUint32(want[8:12], dl)

	if !bytes.Equal(got, want) {
		t.Fatalf("PackHeader mismatch:\n got  %x\n want %x", got, want)
	}
	if len(got) != HeaderSize {
		t.Fatalf("PackHeader size = %d, want %d", len(got), HeaderSize)
	}
}

// TestUnpackHeader_RoundTrip 验证 UnpackHeader 是 PackHeader 的逆运算。
func TestUnpackHeader_RoundTrip(t *testing.T) {
	cases := []Header{
		{X: 0, Y: 0, W: 320, H: 170, DataLen: 108800},  // 满屏
		{X: 10, Y: 20, W: 30, H: 40, DataLen: 2400},
		{X: HeartbeatX, Y: HeartbeatY, W: 0, H: 0, DataLen: HeartbeatDataLen},
		{X: 0xFFFF, Y: 0xFFFF, W: 0xFFFF, H: 0xFFFF, DataLen: 0xFFFFFFFF},
	}
	for _, h := range cases {
		enc := PackHeader(h)
		got, err := UnpackHeader(enc)
		if err != nil {
			t.Fatalf("UnpackHeader: %v", err)
		}
		if got != h {
			t.Fatalf("round trip mismatch: got %+v want %+v", got, h)
		}
	}
}

// TestUnpackHeader_TooShort 验证缓冲区过短时返回错误。
func TestUnpackHeader_TooShort(t *testing.T) {
	if _, err := UnpackHeader(make([]byte, HeaderSize-1)); err == nil {
		t.Fatal("expected error for short buffer")
	}
}

// TestHeartbeatPacket 验证心跳包内容与 ESP32 端识别条件完全一致。
func TestHeartbeatPacket(t *testing.T) {
	pkt := HeartbeatPacket()
	if len(pkt) != HeaderSize {
		t.Fatalf("heartbeat size = %d, want %d", len(pkt), HeaderSize)
	}
	h, err := UnpackHeader(pkt)
	if err != nil {
		t.Fatal(err)
	}
	if !h.IsHeartbeat() {
		t.Fatalf("heartbeat not detected: %+v", h)
	}
	// 与 Python `struct.pack('!HHHH I', 0xFFFF, 0xFFFF, 0, 0, 0)` 等价
	want := []byte{0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
	if !bytes.Equal(pkt, want) {
		t.Fatalf("heartbeat bytes mismatch:\n got  %x\n want %x", pkt, want)
	}
}

// TestPackRectPacket_LengthMismatch 验证 RGB565 长度错误时返回错误。
func TestPackRectPacket_LengthMismatch(t *testing.T) {
	if _, err := PackRectPacket(0, 0, 10, 10, make([]byte, 199)); err == nil {
		t.Fatal("expected length mismatch error")
	}
	if _, err := PackRectPacket(0, 0, 0, 10, []byte{}); err == nil {
		t.Fatal("expected invalid rect error")
	}
}
