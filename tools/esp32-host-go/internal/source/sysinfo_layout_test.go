//go:build windows

package source

import (
	"context"
	"image/color"
	"testing"

	"esp32_host/internal/config"
)

// TestSysInfoLayout_AbsoluteCoords 验证配置了 x/y/w/h 的 widget 按绝对坐标渲染。
// 断言与真实使用率无关：bar 区域内是 bar 色（灰背景或绿前景），
// 区域外是黑色。绝对布局 bar 仅延伸到 x≈64，栈式布局延伸到 x≈318，
// 因此用 x=100 处的颜色即可区分两种布局。
func TestSysInfoLayout_AbsoluteCoords(t *testing.T) {
	const tw, th = 320, 170
	src := NewSysInfoSource(config.SysInfoOpts{
		RefreshMs: 1000,
		Widgets: []config.SysInfoWidget{
			{Type: "disk", X: 5, Y: 150, W: 60, H: 15},
		},
	}, tw, th)
	defer src.Close()

	img, err := src.Next(context.Background())
	if err != nil {
		t.Fatalf("Next: %v", err)
	}

	// 绝对布局：bar 位于 y ≈ 150+15*0.45 ≈ 156.75..162.75, x = 6..64
	if !isBarColor(img.NRGBAAt(30, 160)) {
		t.Fatalf("expected bar color at (30,160), got %+v", img.NRGBAAt(30, 160))
	}
	// x=100 超出绝对布局 bar 范围 → 黑色（栈式布局下此处会是 bar 色）
	if !isBlack(img.NRGBAAt(100, 160)) {
		t.Fatalf("expected black at (100,160) in absolute layout, got %+v", img.NRGBAAt(100, 160))
	}
	// y=78 是栈式布局的 bar 位置，绝对布局下应为黑色
	if !isBlack(img.NRGBAAt(30, 78)) {
		t.Fatalf("expected black at (30,78) in absolute layout, got %+v", img.NRGBAAt(30, 78))
	}
}

// TestSysInfoLayout_StackedDefault 验证无坐标 widget 仍走栈式均分布局。
func TestSysInfoLayout_StackedDefault(t *testing.T) {
	const tw, th = 320, 170
	src := NewSysInfoSource(config.SysInfoOpts{
		RefreshMs: 1000,
		Widgets: []config.SysInfoWidget{
			{Type: "disk"}, // 无坐标
		},
	}, tw, th)
	defer src.Close()

	img, err := src.Next(context.Background())
	if err != nil {
		t.Fatalf("Next: %v", err)
	}

	// 栈式布局：单 widget 占满画布，bar 位于 y ≈ 76.5..82.5, x = 1..319
	if !isBarColor(img.NRGBAAt(30, 78)) {
		t.Fatalf("expected bar color at (30,78) in stacked layout, got %+v", img.NRGBAAt(30, 78))
	}
	// y=160 在栈式 bar 之外 → 黑色（绝对布局下此处会是 bar 色）
	if !isBlack(img.NRGBAAt(30, 160)) {
		t.Fatalf("expected black at (30,160) in stacked layout, got %+v", img.NRGBAAt(30, 160))
	}
}

// isBarColor 判断像素是否为进度条颜色：背景灰 (0x33) 或前景绿 (G=0xFF)。
func isBarColor(c color.NRGBA) bool {
	if c.R == 0x33 && c.G == 0x33 && c.B == 0x33 {
		return true
	}
	return c.R == 0 && c.G == 0xFF && c.B == 0
}

// isBlack 判断像素是否为纯黑背景。
func isBlack(c color.NRGBA) bool {
	return c == (color.NRGBA{R: 0, G: 0, B: 0, A: 255})
}
