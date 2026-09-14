package pipeline

import (
	"image"
	"image/color"
	"testing"
)

// TestFindDirtyRect_NoChange 相同图像无变化。
func TestFindDirtyRect_NoChange(t *testing.T) {
	a := image.NewNRGBA(image.Rect(0, 0, 10, 10))
	for y := 0; y < 10; y++ {
		for x := 0; x < 10; x++ {
			a.SetNRGBA(x, y, color.NRGBA{R: 100, G: 100, B: 100, A: 255})
		}
	}
	// 同样图像拷贝
	b := image.NewNRGBA(image.Rect(0, 0, 10, 10))
	for y := 0; y < 10; y++ {
		for y := 0; y < 10; y++ {
			for x := 0; x < 10; x++ {
				b.SetNRGBA(x, y, color.NRGBA{R: 100, G: 100, B: 100, A: 255})
			}
		}
	}
	r := FindDirtyRect(a, b, 0)
	if r.Changed {
		t.Fatalf("expected no change, got %+v", r)
	}
}

// TestFindDirtyRect_FullFrame_NilPrev prev == nil → FullFrame。
func TestFindDirtyRect_FullFrame_NilPrev(t *testing.T) {
	curr := image.NewNRGBA(image.Rect(0, 0, 8, 5))
	r := FindDirtyRect(nil, curr, 0)
	if !r.Changed || r.X != 0 || r.Y != 0 || r.W != 8 || r.H != 5 {
		t.Fatalf("expected FullFrame, got %+v", r)
	}
}

// TestFindDirtyRect_FullFrame_SizeMismatch 尺寸不同 → FullFrame。
func TestFindDirtyRect_FullFrame_SizeMismatch(t *testing.T) {
	prev := image.NewNRGBA(image.Rect(0, 0, 4, 4))
	curr := image.NewNRGBA(image.Rect(0, 0, 8, 4))
	r := FindDirtyRect(prev, curr, 0)
	if !r.Changed || r.W != 8 {
		t.Fatalf("expected FullFrame 8x4, got %+v", r)
	}
}

// TestFindDirtyRect_BoundingBox 单像素变化返回 (x,y,1,1)。
func TestFindDirtyRect_BoundingBox(t *testing.T) {
	prev := image.NewNRGBA(image.Rect(0, 0, 10, 10))
	curr := image.NewNRGBA(image.Rect(0, 0, 10, 10))
	for y := 0; y < 10; y++ {
		for x := 0; x < 10; x++ {
			prev.SetNRGBA(x, y, color.NRGBA{R: 0, G: 0, B: 0, A: 255})
			curr.SetNRGBA(x, y, color.NRGBA{R: 0, G: 0, B: 0, A: 255})
		}
	}
	// 修改 (3, 4) → (7, 6) 范围
	for y := 4; y <= 6; y++ {
		for x := 3; x <= 7; x++ {
			curr.SetNRGBA(x, y, color.NRGBA{R: 255, G: 255, B: 255, A: 255})
		}
	}
	// threshold=0 → 任何差值都算变化
	r := FindDirtyRect(prev, curr, 0)
	want := DirtyRect{X: 3, Y: 4, W: 5, H: 3, Changed: true}
	if r != want {
		t.Fatalf("got %+v, want %+v", r, want)
	}
}

// TestFindDirtyRect_ThresholdFilter 阈值过滤小变化。
func TestFindDirtyRect_ThresholdFilter(t *testing.T) {
	prev := image.NewNRGBA(image.Rect(0, 0, 4, 1))
	curr := image.NewNRGBA(image.Rect(0, 0, 4, 1))
	prev.SetNRGBA(2, 0, color.NRGBA{R: 0, G: 0, B: 0, A: 255})
	curr.SetNRGBA(2, 0, color.NRGBA{R: 1, G: 0, B: 0, A: 255}) // 差 1
	// threshold=1 不会触发 (d=1 not > 1)
	r := FindDirtyRect(prev, curr, 1)
	if r.Changed {
		t.Fatalf("expected no change at threshold=1, got %+v", r)
	}
	// threshold=0 触发
	r = FindDirtyRect(prev, curr, 0)
	if !r.Changed || r.X != 2 || r.Y != 0 || r.W != 1 || r.H != 1 {
		t.Fatalf("expected 1x1 at (2,0), got %+v", r)
	}
}

// TestFindDirtyRect_NilCurr 不应 panic。
func TestFindDirtyRect_NilCurr(t *testing.T) {
	r := FindDirtyRect(nil, nil, 0)
	if r.Changed {
		t.Fatalf("expected no change, got %+v", r)
	}
}
