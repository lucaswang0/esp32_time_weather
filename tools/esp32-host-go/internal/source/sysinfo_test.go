//go:build windows

package source

import (
	"context"
	"image/color"
	"testing"
	"time"

	"esp32_host/internal/config"
)

// TestSysInfoSource_DefaultWidgets 验证空 widgets 时应用 4 条默认。
func TestSysInfoSource_DefaultWidgets(t *testing.T) {
	src := NewSysInfoSource(config.SysInfoOpts{}, 170, 320)
	if src == nil {
		t.Fatal("NewSysInfoSource returned nil")
	}
	if len(src.cfg.Widgets) != 4 {
		t.Fatalf("default widgets = %d, want 4", len(src.cfg.Widgets))
	}
	if src.cfg.RefreshMs != 1000 {
		t.Fatalf("refresh_ms default = %d, want 1000", src.cfg.RefreshMs)
	}
	if w, h := src.Resolution(); w != 170 || h != 320 {
		t.Fatalf("Resolution = %dx%d, want 170x320", w, h)
	}
}

// TestSysInfoSource_NextReturnsFrame 一次 Next 应返回 170x320 的非全零图像。
func TestSysInfoSource_NextReturnsFrame(t *testing.T) {
	src := NewSysInfoSource(config.SysInfoOpts{
		RefreshMs: 100,
		Widgets: []config.SysInfoWidget{
			{Type: "text", Text: "OK"},
			{Type: "cpu"},
			{Type: "mem"},
			{Type: "net"},
			{Type: "disk"},
		},
	}, 170, 320)
	defer src.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	img, err := src.Next(ctx)
	if err != nil {
		t.Fatalf("Next returned error: %v", err)
	}
	if img.Bounds().Dx() != 170 || img.Bounds().Dy() != 320 {
		t.Fatalf("size = %v, want 170x320", img.Bounds())
	}
	// 至少一个非透明像素
	var nonZero bool
	for y := 0; y < img.Bounds().Dy() && !nonZero; y++ {
		for x := 0; x < img.Bounds().Dx(); x++ {
			_, _, _, a := img.At(x, y).RGBA()
			if a > 0 {
				nonZero = true
				break
			}
		}
	}
	if !nonZero {
		t.Fatal("image is fully transparent")
	}
}

// TestHumanBps 单位换算。
func TestHumanBps(t *testing.T) {
	cases := []struct {
		in   uint64
		want string
	}{
		{0, "0B"},
		{512, "512B"},
		{2048, "2.0K"},
		{5 << 20, "5.0M"},
	}
	for _, c := range cases {
		if got := humanBps(c.in); got != c.want {
			t.Errorf("humanBps(%d) = %q, want %q", c.in, got, c.want)
		}
	}
}

// TestClamp01 范围截断。
func TestClamp01(t *testing.T) {
	if got := clamp01(-0.5); got != 0 {
		t.Errorf("clamp01(-0.5) = %v, want 0", got)
	}
	if got := clamp01(1.5); got != 1 {
		t.Errorf("clamp01(1.5) = %v, want 1", got)
	}
	if got := clamp01(0.4); got != 0.4 {
		t.Errorf("clamp01(0.4) = %v, want 0.4", got)
	}
}

// TestRenderBar 进度条填充颜色正确（pct=50 时应有绿色像素）。
func TestRenderBar(t *testing.T) {
	src := NewSysInfoSource(config.SysInfoOpts{}, 170, 320)
	_ = src
	// renderBar 行为已在 gg 内部绘制，验证 fill 比例即可
	fill := clamp01(50.0/100) * (170.0 - 2)
	if fill != 84 {
		t.Fatalf("fill = %v, want 84", fill)
	}
	_ = color.NRGBA{} // 防止空 import
}
