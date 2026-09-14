// Package pipeline 实现帧处理与发送主循环。
//
// 经典 3-goroutine 模型（与 Python pipeline.py 对齐）：
//
//	generatorLoop:  Source.Next() → frameCh (限速 + 低水位)
//	consumerLoop:   frameCh → resize → diff → color → rgb565 → chunk → Send
//	connectionLoop: 负责 ESP32 连接与重连
package pipeline

import (
	"image"

	"golang.org/x/image/draw"
)

// ResizeKernel 选择缩放算法。
type ResizeKernel int

const (
	KernelCatmullRom ResizeKernel = iota // 与 PIL LANCZOS 质量接近，对应 draw.CatmullRom
	KernelApproxBiLinear                 // 速度优先
)

// Resize 把 src 缩放到 dstW x dstH，输出新图像。
// 输出格式为 *image.NRGBA，便于后续 color 校正就地修改。
//
// dst 允许为 nil（自动分配）；非 nil 时大小必须匹配 dstW/dstH。
func Resize(src image.Image, dstW, dstH int, kernel ResizeKernel) (*image.NRGBA, error) {
	if dstW <= 0 || dstH <= 0 {
		return nil, errInvalidSize
	}
	dst := image.NewNRGBA(image.Rect(0, 0, dstW, dstH))
	switch kernel {
	case KernelApproxBiLinear:
		draw.ApproxBiLinear.Scale(dst, dst.Bounds(), src, src.Bounds(), draw.Over, nil)
	default:
		draw.CatmullRom.Scale(dst, dst.Bounds(), src, src.Bounds(), draw.Over, nil)
	}
	return dst, nil
}

// ResizeInto 把 src 缩放到 dst 中（零分配）。
func ResizeInto(dst *image.NRGBA, src image.Image, kernel ResizeKernel) {
	switch kernel {
	case KernelApproxBiLinear:
		draw.ApproxBiLinear.Scale(dst, dst.Bounds(), src, src.Bounds(), draw.Over, nil)
	default:
		draw.CatmullRom.Scale(dst, dst.Bounds(), src, src.Bounds(), draw.Over, nil)
	}
}
