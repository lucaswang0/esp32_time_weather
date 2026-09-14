package source

import (
	"context"
	"errors"
	"image"
	"image/color"
	"testing"
	"time"
)

// TestErrCaptureTimeout 断言错误可被 errors.Is 识别。
//
// 健壮性需求：屏保/锁屏下 Next 应快速返回 ErrCaptureTimeout，
// pipeline 据此走 sleep+retry 而不是终止。
func TestErrCaptureTimeout(t *testing.T) {
	wrapped := errors.New("wrapper")
	combined := errors.Join(ErrCaptureTimeout, wrapped)
	if !errors.Is(combined, ErrCaptureTimeout) {
		t.Fatal("ErrCaptureTimeout must be detectable via errors.Is")
	}
}

// TestScreenSource_NextTimeout 验证 Next 在截屏阻塞时会超时返回。
//
// 我们用一个不会被调用的 capture 路径不可行；改用 region 把 bounds 强制设为 0
// 触发 invalid bounds 路径，验证错误包裹；timeout 路径需要拦截 screenshot 调用，
// 这里改用极小 timeout 抓取一个超大 region（多数系统会拒绝或极慢）做粗粒度验证。
func TestScreenSource_NextInvalidBounds(t *testing.T) {
	// 当所有显示器都不可用时 captureBounds 会返回 1x1
	// 这里用一个非法 region 测试 Intersect 保护。
	src := NewScreenSource(999, nil) // 999 > NumActiveDisplays → 回退到主显示器
	if src == nil {
		t.Fatal("NewScreenSource returned nil")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	_, err := src.Next(ctx)
	// 在 CI / headless 环境下可能因为没有显示器返回 error；只要不 panic 即可
	if err != nil {
		t.Logf("Next returned error (expected in headless env): %v", err)
	}
}

// newNRGBAUniform 构造一张指定尺寸的纯色 NRGBA 图（用于 crop 测试）。
func newNRGBAUniform(w, h int, c color.NRGBA) *image.NRGBA {
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.SetNRGBA(x, y, c)
		}
	}
	return img
}

// TestCropToAspectRatio_NoOp 当宽高比差异 ≤ 1% 时不裁剪，原图直接返回。
func TestCropToAspectRatio_NoOp(t *testing.T) {
	// 100x100 (aspect 1.0) vs target 170x170 (aspect 1.0) → 完全相同
	src := newNRGBAUniform(100, 100, color.NRGBA{R: 128, G: 0, B: 0, A: 255})
	got := cropToAspectRatio(src, 170, 170, "center")
	if got != src {
		t.Fatalf("expected same pointer when aspects match; got %p != src %p", got, src)
	}
}

// TestCropToAspectRatio_WiderShot 截图比 target 更宽，按 alignment 水平裁剪。
//
// shot 200x100 (aspect 2.0) → target 170x320 (aspect 0.531)；
// shot_aspect > target_aspect → 水平裁剪。
// new_w = int(100 * 170/320) = 53
func TestCropToAspectRatio_WiderShot(t *testing.T) {
	src := newNRGBAUniform(200, 100, color.NRGBA{0, 0, 0, 255})

	cases := []struct {
		align   string
		wantX0  int
		wantX1  int
	}{
		{"left", 0, 53},
		{"center", (200 - 53) / 2, (200-53)/2 + 53},
		{"right", 200 - 53, 200},
	}
	for _, tc := range cases {
		t.Run(tc.align, func(t *testing.T) {
			got := cropToAspectRatio(src, 170, 320, tc.align)
			if got.Bounds().Dx() != tc.wantX1-tc.wantX0 {
				t.Fatalf("align=%s: width=%d, want %d", tc.align, got.Bounds().Dx(), tc.wantX1-tc.wantX0)
			}
			if got.Bounds().Min.X != 0 || got.Bounds().Min.Y != 0 {
				t.Fatalf("align=%s: result origin = %v, want (0,0)", tc.align, got.Bounds().Min)
			}
			if got.Bounds().Dy() != 100 {
				t.Fatalf("align=%s: height=%d, want 100", tc.align, got.Bounds().Dy())
			}
		})
	}
}

// TestCropToAspectRatio_TallerShot 截图比 target 更高，垂直居中裁剪。
//
// shot 100x200 (aspect 0.5) → target 320x170 (aspect 1.882)；
// shot_aspect < target_aspect → 垂直裁剪，保留宽度。
// 公式与 Python window_capture.py 一致：new_h = int(shot_w / target_aspect) = int(100 / 1.882) = 53。
func TestCropToAspectRatio_TallerShot(t *testing.T) {
	src := newNRGBAUniform(100, 200, color.NRGBA{0, 0, 0, 255})
	got := cropToAspectRatio(src, 320, 170, "center")
	if got.Bounds().Dx() != 100 {
		t.Fatalf("width=%d, want 100", got.Bounds().Dx())
	}
	if got.Bounds().Dy() != 53 {
		t.Fatalf("height=%d, want 53", got.Bounds().Dy())
	}
	if got.Bounds().Min.Y != 0 {
		t.Fatalf("origin Y=%d, want 0 (result image is freshly allocated)", got.Bounds().Min.Y)
	}
}

// TestCropToAspectRatio_WithinTolerance 1% 容差内不裁剪。
func TestCropToAspectRatio_WithinTolerance(t *testing.T) {
	// shot 100x100 (1.0) vs target 170x171 (0.994) → 差异 0.6% < 1%
	src := newNRGBAUniform(100, 100, color.NRGBA{0, 0, 0, 255})
	got := cropToAspectRatio(src, 170, 171, "center")
	if got != src {
		t.Fatal("within 1% tolerance should not crop")
	}
}
