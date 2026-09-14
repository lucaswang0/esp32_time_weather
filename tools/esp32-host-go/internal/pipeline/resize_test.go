package pipeline

import (
	"image"
	"image/color"
	"testing"
)

func newTestImage(w, h int, c color.NRGBA) *image.NRGBA {
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.SetNRGBA(x, y, c)
		}
	}
	return img
}

// TestResize_CatmullRom 缩放到目标尺寸。
func TestResize_CatmullRom(t *testing.T) {
	src := newTestImage(100, 50, color.NRGBA{R: 200, G: 100, B: 50, A: 255})
	dst, err := Resize(src, 170, 320, KernelCatmullRom)
	if err != nil {
		t.Fatal(err)
	}
	if dst.Bounds().Dx() != 170 || dst.Bounds().Dy() != 320 {
		t.Fatalf("dst size = %dx%d, want 170x320", dst.Bounds().Dx(), dst.Bounds().Dy())
	}
	// 缩放后单色图像所有像素应相同
	c := dst.NRGBAAt(0, 0)
	if c.R != 200 || c.G != 100 || c.B != 50 {
		t.Fatalf("color = %+v, want {200,100,50,255}", c)
	}
}

// TestResize_InvalidSize 验证非法尺寸报错。
func TestResize_InvalidSize(t *testing.T) {
	src := newTestImage(10, 10, color.NRGBA{R: 0, G: 0, B: 0, A: 255})
	if _, err := Resize(src, 0, 10, KernelCatmullRom); err == nil {
		t.Fatal("expected error for w=0")
	}
	if _, err := Resize(src, 10, -1, KernelCatmullRom); err == nil {
		t.Fatal("expected error for h=-1")
	}
}

// TestResizeInto_Preallocated 测试零分配 resize。
func TestResizeInto_Preallocated(t *testing.T) {
	src := newTestImage(50, 50, color.NRGBA{R: 10, G: 20, B: 30, A: 255})
	dst := image.NewNRGBA(image.Rect(0, 0, 25, 25))
	ResizeInto(dst, src, KernelApproxBiLinear)
	c := dst.NRGBAAt(12, 12)
	if c.R != 10 || c.G != 20 || c.B != 30 {
		t.Fatalf("expected uniform color, got %+v", c)
	}
}
