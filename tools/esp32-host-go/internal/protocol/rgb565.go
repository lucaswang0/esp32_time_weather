package protocol

import (
	"fmt"
	"image"
	"image/color"
	"math"
)

// EncodeRGB565 把目标区域 (rect) 的像素打包为 RGB565 little-endian 字节流。
// 与 Python pipeline.py 中 rgb_to_rgb565 + struct.pack_into('<H', ...) 字节级一致。
//
// 公式：RGB565 = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)
// 字节序：每个像素 2 字节，低位在前 (little-endian uint16)，与 ESP32
// TFT_eSPI setSwapBytes(true) 读取行为互为逆运算。
//
// src 必须覆盖 (x, y, x+w, y+h)；越界会返回错误，避免静默越界写入。
// 输出长度恒为 w*h*2。
func EncodeRGB565(src *image.NRGBA, x, y, w, h int) ([]byte, error) {
	if w <= 0 || h <= 0 {
		return nil, fmt.Errorf("protocol.EncodeRGB565: invalid size w=%d h=%d", w, h)
	}
	if x < 0 || y < 0 || x+w > src.Bounds().Dx() || y+h > src.Bounds().Dy() {
		return nil, fmt.Errorf("protocol.EncodeRGB565: rect out of bounds (x=%d y=%d w=%d h=%d, src=%dx%d)",
			x, y, w, h, src.Bounds().Dx(), src.Bounds().Dy())
	}

	out := make([]byte, w*h*2)
	off := 0
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			c := src.NRGBAAt(x+col, y+row)
			rgb := packRGB565(c.R, c.G, c.B)
			out[off] = byte(rgb)        // low byte
			out[off+1] = byte(rgb >> 8) // high byte
			off += 2
		}
	}
	return out, nil
}

// packRGB565 单像素转换：与 Python 版完全相同的位运算顺序。
func packRGB565(r, g, b uint8) uint16 {
	return (uint16(r&0xF8) << 8) | (uint16(g&0xFC) << 3) | uint16(b>>3)
}

// ApplyGammaAndWhiteBalance 就地对 NRGBA 图像做 gamma + 白平衡校正。
// 与 Python pipeline.py 中 _apply_gamma_and_white_balance 等价。
//
// gamma > 1.0 画面更亮；wbScale 分别缩放 R/G/B 通道（>1 增强）。
// 通道值会被 clamp 到 [0, 255]。
func ApplyGammaAndWhiteBalance(src *image.NRGBA, gamma float64, wbScale [3]float64) {
	if gamma == 1.0 && wbScale == [3]float64{1, 1, 1} {
		return
	}
	for i := 0; i < len(src.Pix); i += 4 {
		r := float64(src.Pix[i+0]) / 255.0
		g := float64(src.Pix[i+1]) / 255.0
		b := float64(src.Pix[i+2]) / 255.0

		if gamma != 1.0 {
			r = powPreserveSign(r, gamma)
			g = powPreserveSign(g, gamma)
			b = powPreserveSign(b, gamma)
		}

		r *= wbScale[0]
		g *= wbScale[1]
		b *= wbScale[2]

		src.Pix[i+0] = clampU8(r * 255.0)
		src.Pix[i+1] = clampU8(g * 255.0)
		src.Pix[i+2] = clampU8(b * 255.0)
	}
}

// powPreserveSign math.Pow 的 0 边界保护（伽马函数对 0 应保持 0）。
func powPreserveSign(v, gamma float64) float64 {
	if v <= 0 {
		return 0
	}
	return math.Pow(v, gamma)
}

// clampU8 float 转 [0, 255] uint8。
func clampU8(v float64) uint8 {
	if v < 0 {
		return 0
	}
	if v > 255 {
		return 255
	}
	return uint8(v)
}

// NRGBAFromImage 统一转换为 *image.NRGBA，便于后续就地修改。
// 若 src 已经是 *image.NRGBA 则直接返回零拷贝。
func NRGBAFromImage(src image.Image) *image.NRGBA {
	if n, ok := src.(*image.NRGBA); ok {
		return n
	}
	b := src.Bounds()
	out := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		for x := 0; x < b.Dx(); x++ {
			r, g, bb, a := src.At(b.Min.X+x, b.Min.Y+y).RGBA()
			c := color.NRGBA{R: uint8(r >> 8), G: uint8(g >> 8), B: uint8(bb >> 8), A: uint8(a >> 8)}
			out.SetNRGBA(x, y, c)
		}
	}
	return out
}
