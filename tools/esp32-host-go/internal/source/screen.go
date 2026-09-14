package source

import (
	"context"
	"errors"
	"fmt"
	"image"
	"sync"
	"time"

	"github.com/kbinani/screenshot"
)

// defaultCaptureTimeout 单次截屏调用的默认超时。
//
// 经验值：正常运行下 1080p 抓帧 < 30ms；超时 2s 足以覆盖短暂卡顿，
// 也能在屏保/锁屏下快速返回而非无限挂起。
const defaultCaptureTimeout = 2 * time.Second

// ScreenSource 使用 kbinani/screenshot 抓取指定显示器或子区域。
//
// 配置：
//   - monitor: 0 表示自动选主显示器，1+ 表示 Display N (N>=1)
//   - region:  非空时只截取该子区域；空时截整个显示器
//
// 抓取到的图像保持原生分辨率，由 pipeline 在 consumer 端缩放到目标尺寸。
type ScreenSource struct {
	monitor   int
	region    *image.Rectangle
	timeout   time.Duration

	mu sync.Mutex // 避免多 goroutine 同时调用 screenshot API
}

// NewScreenSource 构造桌面截屏源。
// monitor<0 视为 0；monitor=0 表示自动选主显示器。
// region 为 nil 时截整个显示器。
// timeout<=0 时使用 defaultCaptureTimeout。
func NewScreenSource(monitor int, region *image.Rectangle) *ScreenSource {
	return NewScreenSourceWithTimeout(monitor, region, 0)
}

// NewScreenSourceWithTimeout 同 NewScreenSource，但允许调用方指定单次抓帧超时。
func NewScreenSourceWithTimeout(monitor int, region *image.Rectangle, timeout time.Duration) *ScreenSource {
	if monitor < 0 {
		monitor = 0
	}
	if timeout <= 0 {
		timeout = defaultCaptureTimeout
	}
	return &ScreenSource{monitor: monitor, region: region, timeout: timeout}
}

// Resolution 返回原生分辨率（不是目标分辨率——缩放由 pipeline 完成）。
// 第一次调用会触发显示器枚举；之后缓存。
func (s *ScreenSource) Resolution() (int, int) {
	b := s.captureBounds()
	return b.Dx(), b.Dy()
}

// Next 抓取一帧。
//
// 实现注意：
//   - kbinani/screenshot 在屏保/锁屏/多显示器热插拔窗口期可能阻塞，
//     这里用独立 goroutine + timer 包裹；超过 s.timeout 返回 ErrCaptureTimeout。
//   - 显示器数量变化（NumActiveDisplays 变小）会导致旧索引越界，
//     captureBounds 已自动夹断到主显示器。
func (s *ScreenSource) Next(ctx context.Context) (*image.NRGBA, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	s.mu.Lock()
	bounds := s.captureBounds()
	s.mu.Unlock()

	// 退化情况：无显示器或全零区域
	if bounds.Dx() <= 0 || bounds.Dy() <= 0 {
		return nil, fmt.Errorf("screen: invalid bounds %v", bounds)
	}

	type result struct {
		img *image.RGBA
		err error
	}
	done := make(chan result, 1)
	go func() {
		// screenshot 包内部对并发调用有锁；但放进独立 goroutine
		// 是为了让 timer 在它阻塞时能 fire。
		img, err := screenshot.CaptureRect(bounds)
		done <- result{img: img, err: err}
	}()

	timer := time.NewTimer(s.timeout)
	defer timer.Stop()
	select {
	case r := <-done:
		if r.err != nil {
			return nil, fmt.Errorf("screen.CaptureRect: %w", r.err)
		}
		if r.img == nil {
			return nil, errors.New("screen: CaptureRect returned nil image")
		}
		return rgbaToNRGBA(r.img), nil
	case <-ctx.Done():
		return nil, ctx.Err()
	case <-timer.C:
		return nil, fmt.Errorf("%w (after %s)", ErrCaptureTimeout, s.timeout)
	}
}

// Close 释放资源（kbinani/screenshot 无显式 Close，保留方法以满足接口）。
func (s *ScreenSource) Close() error {
	return nil
}

// captureBounds 解析最终截屏区域。
//
// 每次都重新查 NumActiveDisplays，使热插拔/数量变化立即生效。
// 当请求的 monitor 索引超出当前活动数时，回退到主显示器而不是返回越界区域。
func (s *ScreenSource) captureBounds() image.Rectangle {
	n := screenshot.NumActiveDisplays()
	if n <= 0 {
		return image.Rect(0, 0, 1, 1)
	}
	// 0 = 自动选主显示器（按 NumActiveDisplays 顺序的第一个）
	idx := s.monitor
	if idx <= 0 || idx > n {
		idx = 0
	} else {
		idx-- // monitor=1 -> 索引 0
	}
	bounds := screenshot.GetDisplayBounds(idx)
	if s.region != nil {
		// region 是相对显示器原点的偏移+尺寸
		r := image.Rect(
			bounds.Min.X+s.region.Min.X,
			bounds.Min.Y+s.region.Min.Y,
			bounds.Min.X+s.region.Max.X,
			bounds.Min.Y+s.region.Max.Y,
		)
		// 裁剪到显示器范围内（避免热插拔后 region 越界）
		return r.Intersect(bounds)
	}
	return bounds
}

// rgbaToNRGBA image.RGBA → *image.NRGBA 拷贝。
// kbinani/screenshot 的 CaptureRect 在不同平台上可能返回 RGBA 或 NRGBA；
// 这里统一做安全拷贝，避免假设底层类型。
func rgbaToNRGBA(src *image.RGBA) *image.NRGBA {
	b := src.Bounds()
	out := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		si := src.PixOffset(b.Min.X, b.Min.Y+y)
		di := out.PixOffset(0, y)
		copy(out.Pix[di:di+4*b.Dx()], src.Pix[si:si+4*b.Dx()])
	}
	return out
}
