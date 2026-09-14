//go:build !windows

package source

import (
	"context"
	"image"

	"esp32_host/internal/config"
)

// NewWindowSource 非 Windows 平台返回错误占位。
// targetW/targetH 在非 Windows 平台无意义，签名仅为与 window_windows.go 对齐。
func NewWindowSource(cfg config.WindowOpts, targetW, targetH int) Source {
	return &unsupportedSource{platform: "non-Windows"}
}

type unsupportedSource struct {
	platform string
}

func (u *unsupportedSource) Resolution() (int, int) { return 0, 0 }
func (u *unsupportedSource) Next(ctx context.Context) (*image.NRGBA, error) {
	return nil, ErrUnsupported
}
func (u *unsupportedSource) Close() error { return nil }
