//go:build windows

package source

import (
	"context"
	"fmt"
	"image"
	"image/color"
	"sync"
	"time"

	"github.com/fogleman/gg"
	"github.com/shirou/gopsutil/v3/cpu"
	"github.com/shirou/gopsutil/v3/disk"
	"github.com/shirou/gopsutil/v3/mem"
	"github.com/shirou/gopsutil/v3/net"

	"esp32_host/internal/config"
)

// SysInfoSource 用 gopsutil 采集 + gg 绘制仪表盘。
// Next 返回的图像即 targetW x targetH，无需 pipeline 再缩放。
type SysInfoSource struct {
	cfg     config.SysInfoOpts
	targetW int
	targetH int

	mu       sync.Mutex
	prevNet  net.IOCountersStat // 上次采样；用于速率
	prevTime time.Time
	closed   bool
}

// NewSysInfoSource 构造系统仪表盘源。
// targetW/targetH 决定输出图像尺寸；refreshMs<=0 则用 config 默认值 1000。
func NewSysInfoSource(cfg config.SysInfoOpts, targetW, targetH int) *SysInfoSource {
	if targetW <= 0 {
		targetW = 170
	}
	if targetH <= 0 {
		targetH = 320
	}
	if cfg.RefreshMs <= 0 {
		cfg.RefreshMs = 1000
	}
	if len(cfg.Widgets) == 0 {
		cfg.Widgets = []config.SysInfoWidget{
			{Type: "cpu"},
			{Type: "mem"},
			{Type: "net"},
			{Type: "disk"},
		}
	}
	return &SysInfoSource{cfg: cfg, targetW: targetW, targetH: targetH}
}

func (s *SysInfoSource) Resolution() (int, int) { return s.targetW, s.targetH }

// Next 渲染一帧仪表盘。出错时返回错误，由 pipeline 决定是否重试。
// 网络/磁盘失败不视为致命；该 widget 显示 N/A。
func (s *SysInfoSource) Next(ctx context.Context) (*image.NRGBA, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.closed {
		return nil, ErrUnsupported
	}

	// 渲染
	dc := gg.NewContext(s.targetW, s.targetH)
	dc.SetColor(color.Black)
	dc.Clear()
	if err := dc.LoadFontFace("arial.ttf", 12); err != nil {
		// 字体加载失败时回退到 gg 内嵌位图字体（不可读但不会 panic）
		_ = dc.LoadFontFace("", 12)
	}

	// 布局：w/h 均大于 0 的 widget 按绝对坐标渲染；
	// 其余按数组顺序在剩余画布上从上到下均分（向后兼容旧配置）。
	stackCount := 0
	for _, w := range s.cfg.Widgets {
		if w.W <= 0 || w.H <= 0 {
			stackCount++
		}
	}
	widgetH := float64(s.targetH) / float64(maxInt(stackCount, 1))
	stackIdx := 0
	for _, w := range s.cfg.Widgets {
		if w.W > 0 && w.H > 0 {
			s.renderWidget(dc, w, float64(w.X), float64(w.Y), float64(w.W), float64(w.H))
			continue
		}
		y := float64(stackIdx) * widgetH
		s.renderWidget(dc, w, 0, y, float64(s.targetW), widgetH)
		stackIdx++
	}
	img := rgbaToNRGBAFromImage(dc.Image())
	return img, nil
}

func (s *SysInfoSource) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.closed = true
	return nil
}

// renderWidget 渲染单个 widget；行高 widgetH，整体居中。
func (s *SysInfoSource) renderWidget(dc *gg.Context, w config.SysInfoWidget, x, y, wW, wH float64) {
	title := w.Name
	if title == "" {
		title = defaultWidgetTitle(w.Type)
	}

	switch w.Type {
	case "text":
		dc.SetColor(color.White)
		dc.DrawStringAnchored(w.Text, wW/2, y+wH/2, 0.5, 0.5)
		return
	case "cpu", "mem", "disk":
		pct := s.samplePercent(w.Type)
		s.renderBar(dc, title, pct, x, y, wW, wH)
	case "net":
		upBps, downBps := s.sampleNetRate()
		dc.SetColor(color.White)
		dc.DrawStringAnchored(title, 1, y+1, 0, 1) // 左上
		dc.DrawStringAnchored(
			fmt.Sprintf("U:%s D:%s", humanBps(upBps), humanBps(downBps)),
			1, y+wH-2, 0, -1,
		)
	default:
		dc.SetColor(color.RGBA{R: 255, A: 255})
		dc.DrawStringAnchored("?"+w.Type, wW/2, y+wH/2, 0.5, 0.5)
	}
}

// renderBar 绘制标题+百分比+进度条。
func (s *SysInfoSource) renderBar(dc *gg.Context, title string, pct float64, x, y, w, h float64) {
	// 标题
	dc.SetColor(color.White)
	dc.DrawStringAnchored(title, x+1, y+1, 0, 1)
	dc.DrawStringAnchored(fmt.Sprintf("%.0f%%", pct), x+w-1, y+1, 1, 1)

	// 进度条（背景 + 前景）
	barY := y + h*0.45
	barH := 6.0
	barW := w - 2
	dc.SetColor(color.RGBA{R: 0x33, G: 0x33, B: 0x33, A: 0xFF})
	dc.DrawRectangle(x+1, barY, barW, barH)
	dc.Fill()
	dc.SetColor(color.RGBA{G: 0xFF, A: 0xFF})
	fill := barW * clamp01(pct/100)
	if fill > 0 {
		dc.DrawRectangle(x+1, barY, fill, barH)
		dc.Fill()
	}
}

// samplePercent 采样 cpu/mem/disk 使用率。
func (s *SysInfoSource) samplePercent(widgetType string) float64 {
	switch widgetType {
	case "cpu":
		pcts, err := cpu.Percent(0, false)
		if err != nil || len(pcts) == 0 {
			return 0
		}
		return pcts[0]
	case "mem":
		vm, err := mem.VirtualMemory()
		if err != nil {
			return 0
		}
		return vm.UsedPercent
	case "disk":
		du, err := disk.Usage("C:")
		if err != nil {
			return 0
		}
		return du.UsedPercent
	}
	return 0
}

// sampleNetRate 来自网络 IO 计数器，返回（上行bps, 下行bps）。
func (s *SysInfoSource) sampleNetRate() (uint64, uint64) {
	io, err := net.IOCounters(false)
	if err != nil || len(io) == 0 {
		return 0, 0
	}
	cur := io[0]
	now := time.Now()
	if s.prevTime.IsZero() {
		s.prevNet, s.prevTime = cur, now
		return 0, 0
	}
	dt := now.Sub(s.prevTime).Seconds()
	if dt <= 0 {
		return 0, 0
	}
	up := uint64(float64(cur.BytesSent-s.prevNet.BytesSent) / dt)
	down := uint64(float64(cur.BytesRecv-s.prevNet.BytesRecv) / dt)
	s.prevNet, s.prevTime = cur, now
	return up, down
}

func defaultWidgetTitle(t string) string {
	switch t {
	case "cpu":
		return "CPU"
	case "mem":
		return "MEM"
	case "net":
		return "NET"
	case "disk":
		return "DISK"
	}
	return t
}

func humanBps(b uint64) string {
	const k = 1024
	switch {
	case b >= 1<<20:
		return fmt.Sprintf("%.1fM", float64(b)/(1<<20))
	case b >= 1<<10:
		return fmt.Sprintf("%.1fK", float64(b)/k)
	}
	return fmt.Sprintf("%dB", b)
}

func clamp01(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// rgbaToNRGBAFromImage 简单拷贝；gg 返回 *image.RGBA。
func rgbaToNRGBAFromImage(rgba image.Image) *image.NRGBA {
	b := rgba.Bounds()
	out := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		for x := 0; x < b.Dx(); x++ {
			out.Set(x, y, rgba.At(b.Min.X+x, b.Min.Y+y))
		}
	}
	return out
}
