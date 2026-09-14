package protocol

import (
	"bytes"
	"encoding/binary"
	"image"
	"image/color"
	"testing"
)

// TestPackRGB565_SinglePixel 验证单像素的 RGB565 转换结果。
//
// Python 等价代码：
//
//	rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
//	struct.pack('<H', rgb)   # little-endian uint16
func TestPackRGB565_SinglePixel(t *testing.T) {
	// (R, G, B) → 期望 RGB565 → 期望 little-endian 字节
	cases := []struct {
		r, g, b uint8
		wantLE  []byte // 2 字节
	}{
		// 红色最大 (R=255,G=0,B=0)
		{255, 0, 0, []byte{0x00, 0xF8}},
		// 绿色最大 (R=0,G=255,B=0)
		{0, 255, 0, []byte{0xE0, 0x07}},
		// 蓝色最大 (R=0,G=0,B=255)
		{0, 0, 255, []byte{0x1F, 0x00}},
		// 白色
		{255, 255, 255, []byte{0xFF, 0xFF}},
		// 黑色
		{0, 0, 0, []byte{0x00, 0x00}},
		// 任意 (R=255, G=128, B=64)
		// r_mask = 0xF8 << 8 = 0xF800
		// g_mask = 0x80 << 3 = 0x0400
		// b_mask = 0x40 >> 3 = 0x08
		// 合计 = 0xFC08, little-endian = 0x08, 0xFC
		{255, 128, 64, []byte{0x08, 0xFC}},
	}
	for _, c := range cases {
		img := image.NewNRGBA(image.Rect(0, 0, 1, 1))
		img.SetNRGBA(0, 0, color.NRGBA{R: c.r, G: c.g, B: c.b, A: 255})
		got, err := EncodeRGB565(img, 0, 0, 1, 1)
		if err != nil {
			t.Fatalf("(%d,%d,%d) err: %v", c.r, c.g, c.b, err)
		}
		if !bytes.Equal(got, c.wantLE) {
			t.Fatalf("(%d,%d,%d) bytes = %x, want %x", c.r, c.g, c.b, got, c.wantLE)
		}
		// 同时验证 packRGB565 与解码后 uint16 一致
		u := binary.LittleEndian.Uint16(got)
		want := packRGB565(c.r, c.g, c.b)
		if u != want {
			t.Fatalf("(%d,%d,%d) uint16 = %#x, want %#x", c.r, c.g, c.b, u, want)
		}
	}
}

// TestPackRGB565_MultiPixel 验证连续多像素的字节序。
func TestPackRGB565_MultiPixel(t *testing.T) {
	img := image.NewNRGBA(image.Rect(0, 0, 2, 1))
	img.SetNRGBA(0, 0, color.NRGBA{R: 255, G: 0, B: 0, A: 255})   // 0xF800 LE = 0x00 0xF8
	img.SetNRGBA(1, 0, color.NRGBA{R: 0, G: 0, B: 255, A: 255})   // 0x001F LE = 0x1F 0x00

	got, err := EncodeRGB565(img, 0, 0, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	want := []byte{0x00, 0xF8, 0x1F, 0x00}
	if !bytes.Equal(got, want) {
		t.Fatalf("got %x want %x", got, want)
	}
}

// TestEncodeRGB565_OutOfBounds 验证越界返回错误。
func TestEncodeRGB565_OutOfBounds(t *testing.T) {
	img := image.NewNRGBA(image.Rect(0, 0, 10, 10))
	if _, err := EncodeRGB565(img, 0, 0, 11, 1); err == nil {
		t.Fatal("expected out-of-bounds error")
	}
	if _, err := EncodeRGB565(img, 0, 0, 0, 1); err == nil {
		t.Fatal("expected invalid size error")
	}
}

// TestApplyGammaAndWhiteBalance 验证 gamma + 白平衡 clamp 行为。
func TestApplyGammaAndWhiteBalance(t *testing.T) {
	img := image.NewNRGBA(image.Rect(0, 0, 1, 1))
	img.SetNRGBA(0, 0, color.NRGBA{R: 100, G: 100, B: 100, A: 255})

	// gamma=1.0, wb=(1,1,1) → 无变化
	ApplyGammaAndWhiteBalance(img, 1.0, [3]float64{1, 1, 1})
	if img.Pix[0] != 100 || img.Pix[1] != 100 || img.Pix[2] != 100 {
		t.Fatalf("identity transform changed pixels: %v", img.Pix[:3])
	}

	// gamma=2.0 → 100/255 ≈ 0.392 → 0.392^2 ≈ 0.154 → *255 ≈ 39
	img2 := image.NewNRGBA(image.Rect(0, 0, 1, 1))
	img2.SetNRGBA(0, 0, color.NRGBA{R: 100, G: 100, B: 100, A: 255})
	ApplyGammaAndWhiteBalance(img2, 2.0, [3]float64{1, 1, 1})
	if img2.Pix[0] < 35 || img2.Pix[0] > 45 {
		t.Fatalf("gamma=2.0 unexpected value: %d (want ~39)", img2.Pix[0])
	}

	// gamma=0.5, wb=(2,1,1) → 100/255 → sqrt ≈ 0.626 → *2*255 ≈ 319 → clamp 255
	img3 := image.NewNRGBA(image.Rect(0, 0, 1, 1))
	img3.SetNRGBA(0, 0, color.NRGBA{R: 100, G: 100, B: 100, A: 255})
	ApplyGammaAndWhiteBalance(img3, 0.5, [3]float64{2, 1, 1})
	if img3.Pix[0] != 255 {
		t.Fatalf("wb clamp expected 255, got %d", img3.Pix[0])
	}
}
