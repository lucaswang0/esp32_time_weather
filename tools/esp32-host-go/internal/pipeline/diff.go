package pipeline

import (
	"image"
)

// DirtyRect 表示一帧的脏矩形。
// Changed=false 表示整帧无变化。
type DirtyRect struct {
	X, Y, W, H int
	Changed    bool
}

// FullFrame 返回覆盖整张图像的脏矩形（用于首帧或尺寸变化时）。
func FullFrame(w, h int) DirtyRect {
	return DirtyRect{X: 0, Y: 0, W: w, H: h, Changed: true}
}

// FindDirtyRect 计算 curr 相对于 prev 的脏矩形（曼哈顿距离版本）。
//
// 与 Python pipeline.py _find_dirty_rects 字节级等价：
//
//	abs_diff = sum(|curr - prev|)  // 沿 channel 轴
//	changed_mask = abs_diff > threshold
//	bounding box of changed pixels  → (minX, minY, maxX-minX+1, maxY-minY+1)
//
// 返回 DirtyRect；无变化时 Changed=false。
//
// 边界：
//   - prev == nil                → FullFrame
//   - prev 与 curr 尺寸不同        → FullFrame
//   - 任何超出图像尺寸的访问都会因 Go 切片 panic 自然失败（不静默越界）
func FindDirtyRect(prev, curr *image.NRGBA, threshold int) DirtyRect {
	if curr == nil {
		return DirtyRect{}
	}
	w, h := curr.Bounds().Dx(), curr.Bounds().Dy()
	if prev == nil || prev.Bounds().Dx() != w || prev.Bounds().Dy() != h {
		return FullFrame(w, h)
	}

	minX, minY := w, h
	maxX, maxY := -1, -1

	for y := 0; y < h; y++ {
		currRow := curr.PixOffset(0, y)
		for x := 0; x < w; x++ {
			// NRGBA 4 字节/像素：R,G,B,A
			i := currRow + x*4
			r := int(curr.Pix[i+0]) - int(prev.Pix[i+0])
			g := int(curr.Pix[i+1]) - int(prev.Pix[i+1])
			b := int(curr.Pix[i+2]) - int(prev.Pix[i+2])
			// |r| + |g| + |b|  — 与 numpy sum(abs(...)) 等价
			d := absI(r) + absI(g) + absI(b)
			if d > threshold {
				if x < minX {
					minX = x
				}
				if x > maxX {
					maxX = x
				}
				if y < minY {
					minY = y
				}
				if y > maxY {
					maxY = y
				}
			}
		}
	}

	if maxX < 0 {
		return DirtyRect{} // 无变化
	}
	return DirtyRect{
		X:       minX,
		Y:       minY,
		W:       maxX - minX + 1,
		H:       maxY - minY + 1,
		Changed: true,
	}
}

func absI(v int) int {
	if v < 0 {
		return -v
	}
	return v
}
