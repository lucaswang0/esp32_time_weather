// Package source 提供帧源抽象。
//
// 每种数据源（屏幕、窗口、系统性能仪表盘）实现 Source 接口，
// 由 pipeline 在自己的生成 goroutine 中调用 Next() 拉取一帧。
package source

import (
	"context"
	"errors"
	"image"
)

// Source 一个帧源。
//
// Next 必须返回当前画面的完整快照；返回的图像大小由 Resolution() 决定。
// 返回的图像应当每次新建（避免跨帧共享内部缓冲导致 diff 比对出错）。
// Close 释放底层资源；多次调用应安全。
type Source interface {
	Resolution() (width, height int)
	Next(ctx context.Context) (*image.NRGBA, error)
	Close() error
}

// ErrUnsupported 当前平台不支持此数据源。
var ErrUnsupported = errors.New("source: not supported on this platform")

// ErrCaptureTimeout 截屏调用超过指定时长。
//
// 常见原因：屏保/锁屏界面下底层截屏 API 被阻塞；多显示器拓扑变更期间系统调用挂起。
// pipeline 收到此错误应 sleep 后重试，不应终止。
var ErrCaptureTimeout = errors.New("source: capture timed out")

// Resize 在 source 包内复用 pipeline 缩放工具的入口；具体实现在 pipeline 子包，
// 此处仅作抽象导入占位，避免循环依赖。
//
// 实际缩放函数由调用方（pipeline）注入，本接口不直接做缩放。
