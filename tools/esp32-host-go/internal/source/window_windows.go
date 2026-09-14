//go:build windows

package source

import (
	"context"
	"errors"
	"fmt"
	"image"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"github.com/kbinani/screenshot"
	"github.com/lxn/win"
	"golang.org/x/sys/windows"

	"esp32_host/internal/config"
)

// windowCaptureTimeout 单次窗口截屏调用超时。与 ScreenSource 一致。
const windowCaptureTimeout = 2 * time.Second

// WindowSource 按窗口标题抓取指定窗口的画面，并按 target 宽高比裁剪后返回。
//
// 行为与 Python tools/Python-ESP32-Stream/window_capture.py 等价：
//   - 用 EnumWindows + GetWindowTextW 找到第一个标题（大小写不敏感）包含 cfg.Title 的可见窗口
//   - GetWindowRect 取窗口屏幕坐标
//   - kbinani/screenshot.CaptureRect 抓帧
//   - 若抓到的截图与 target 宽高比差异 > 1%，按 cfg.CropAlignment 裁剪
//
// 注意：source 仅裁剪到 target 宽高比，不在这里 resize；缩放由 pipeline consumer
// 统一用 CatmullRom 完成（避免重复工作）。
type WindowSource struct {
	cfg     config.WindowOpts
	targetW int
	targetH int
	timeout time.Duration

	mu     sync.Mutex
	hwnd   win.HWND
	lastOK time.Time // 最近一次成功 FindWindow 的时间；用于缓存
}

// NewWindowSource 构造窗口截屏源。
// targetW/targetH 来自 pipeline 的 target.width/height，用于决定裁剪比例。
// timeout<=0 时使用 windowCaptureTimeout。
func NewWindowSource(cfg config.WindowOpts, targetW, targetH int) *WindowSource {
	if targetW <= 0 {
		targetW = 170
	}
	if targetH <= 0 {
		targetH = 320
	}
	if cfg.CropAlignment == "" {
		cfg.CropAlignment = "center"
	}
	return &WindowSource{
		cfg:     cfg,
		targetW: targetW,
		targetH: targetH,
		timeout: windowCaptureTimeout,
	}
}

// Resolution 返回目标分辨率（Next 返回的图像就是该尺寸）。
// 当前 pipeline 不会读取此值，但保持接口一致。
func (w *WindowSource) Resolution() (int, int) {
	return w.targetW, w.targetH
}

// Next 抓取一帧。
//
// 健壮性：
//   - 截屏调用 goroutine+timer 包裹，超时返回 ErrCaptureTimeout
//   - 窗口关闭/最小化时 GetWindowRect 返回 0×0 矩形 → 返回 error 让 pipeline sleep+retry
//   - 多显示器：screenshot.CaptureRect 自动按显示器虚拟屏幕坐标
func (w *WindowSource) Next(ctx context.Context) (*image.NRGBA, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	w.mu.Lock()
	defer w.mu.Unlock()

	bounds, err := w.findWindowBounds()
	if err != nil {
		return nil, err
	}
	if bounds.Dx() <= 0 || bounds.Dy() <= 0 {
		return nil, fmt.Errorf("window: zero-size bounds %v", bounds)
	}

	type result struct {
		img *image.RGBA
		err error
	}
	done := make(chan result, 1)
	go func() {
		img, err := screenshot.CaptureRect(bounds)
		done <- result{img: img, err: err}
	}()

	timer := time.NewTimer(w.timeout)
	defer timer.Stop()
	var captured *image.RGBA
	select {
	case r := <-done:
		if r.err != nil {
			return nil, fmt.Errorf("window.CaptureRect: %w", r.err)
		}
		if r.img == nil {
			return nil, errors.New("window: CaptureRect returned nil image")
		}
		captured = r.img
	case <-ctx.Done():
		return nil, ctx.Err()
	case <-timer.C:
		return nil, fmt.Errorf("%w (after %s)", ErrCaptureTimeout, w.timeout)
	}

	src := rgbaToNRGBA(captured)
	// 裁剪到 target 宽高比，保留 native 像素，由 pipeline 统一 resize。
	return cropToAspectRatio(src, w.targetW, w.targetH, w.cfg.CropAlignment), nil
}

// Close 释放资源（无显式资源）。
func (w *WindowSource) Close() error { return nil }

// findWindowBounds 用缓存 + 重新枚举找到窗口的屏幕坐标。
//
// 缓存策略：连续 5 秒内复用上次的 hwnd；超出则重新 EnumWindows。
// 标题大小写不敏感、子串匹配（与 pygetwindow.getWindowsWithTitle 行为一致）。
func (w *WindowSource) findWindowBounds() (image.Rectangle, error) {
	if w.cfg.Title == "" {
		return image.Rect(0, 0, 1, 1), errors.New("window.title is empty")
	}

	// 1) 尝试复用缓存
	if w.hwnd != 0 && time.Since(w.lastOK) < 5*time.Second {
		if r, ok := queryWindowRect(w.hwnd); ok {
			return r, nil
		}
		// 缓存的 hwnd 已失效（窗口关闭）→ 重新查找
		w.hwnd = 0
	}

	// 2) 重新枚举
	hwnd, err := findVisibleWindowByTitle(w.cfg.Title)
	if err != nil {
		return image.Rect(0, 0, 1, 1), err
	}
	w.hwnd = hwnd
	w.lastOK = time.Now()
	r, ok := queryWindowRect(hwnd)
	if !ok {
		w.hwnd = 0
		return image.Rect(0, 0, 1, 1), fmt.Errorf("window: GetWindowRect failed for hwnd=%v", hwnd)
	}
	return r, nil
}

// queryWindowRect 把 lxn/win.RECT 转成 image.Rectangle；窗口不可见时返回 false。
func queryWindowRect(hwnd win.HWND) (image.Rectangle, bool) {
	if !win.IsWindowVisible(hwnd) {
		return image.Rectangle{}, false
	}
	var r win.RECT
	if !win.GetWindowRect(hwnd, &r) {
		return image.Rectangle{}, false
	}
	return image.Rect(int(r.Left), int(r.Top), int(r.Right), int(r.Bottom)), true
}

// WindowInfo 描述一个可见窗口（供 M5 GUI 列表展示）。
type WindowInfo struct {
	Title string
	HWND  uintptr // win.HWND
}

// ListVisibleWindows 枚举所有可见顶层窗口，按标题排序。
//
// 用于 M5 Fyne GUI 的"选择窗口"下拉框；M3 本身不会调用。
func ListVisibleWindows() ([]WindowInfo, error) {
	var (
		mu    sync.Mutex
		infos []WindowInfo
	)
	cb := syscall.NewCallback(func(hwnd win.HWND, _ uintptr) uintptr {
		if !win.IsWindowVisible(hwnd) {
			return 1
		}
		var buf [256]uint16
		n, err := getWindowTextW(hwnd, &buf[0], int32(len(buf)))
		if err != nil || n == 0 {
			return 1
		}
		title := windows.UTF16ToString(buf[:n])
		if strings.TrimSpace(title) == "" {
			return 1
		}
		mu.Lock()
	infos = append(infos, WindowInfo{Title: title, HWND: uintptr(hwnd)})
	mu.Unlock()
	return 1 // 继续枚举
	})
	var param unsafe.Pointer
	if err := windows.EnumWindows(cb, param); err != nil {
		return nil, fmt.Errorf("EnumWindows: %w", err)
	}
	return infos, nil
}

// findVisibleWindowByTitle 找一个标题包含 target（大小写不敏感）的可见窗口。
// 多个匹配时返回第一个。
func findVisibleWindowByTitle(target string) (win.HWND, error) {
	targetLower := strings.ToLower(target)
	var found win.HWND
	var mu sync.Mutex
	cb := syscall.NewCallback(func(hwnd win.HWND, _ uintptr) uintptr {
		mu.Lock()
		stop := found != 0
		mu.Unlock()
		if stop {
			return 0
		}
		if !win.IsWindowVisible(hwnd) {
			return 1
		}
		var buf [256]uint16
		n, err := getWindowTextW(hwnd, &buf[0], int32(len(buf)))
		if err != nil || n == 0 {
			return 1
		}
		title := windows.UTF16ToString(buf[:n])
		if strings.Contains(strings.ToLower(title), targetLower) {
			mu.Lock()
			if found == 0 {
				found = hwnd
			}
			mu.Unlock()
			return 0 // 找到，停止枚举
		}
		return 1
	})
	var param unsafe.Pointer
	if err := windows.EnumWindows(cb, param); err != nil {
		return 0, fmt.Errorf("EnumWindows: %w", err)
	}
	if found == 0 {
		return 0, fmt.Errorf("no visible window matching title %q", target)
	}
	return found, nil
}

// procGetWindowTextW 懒加载 user32!GetWindowTextW（lxn/win 和 x/sys/windows 都没有）。
var procGetWindowTextW = windows.NewLazySystemDLL("user32.dll").NewProc("GetWindowTextW")

// getWindowTextW 调用 GetWindowTextW。返回写入的字符数（不含末尾 NUL）。
// 失败或窗口无标题时返回 0。
func getWindowTextW(hwnd win.HWND, buf *uint16, n int32) (int32, error) {
	r, _, e1 := syscall.Syscall(
		procGetWindowTextW.Addr(),
		3,
		uintptr(hwnd),
		uintptr(unsafe.Pointer(buf)),
		uintptr(n),
	)
	if r == 0 {
		if e1 != syscall.Errno(0) {
			return 0, error(e1)
		}
		return 0, nil
	}
	return int32(r), nil
}

// cropToAspectRatio 把 src 裁剪到 (targetW x targetH) 的宽高比。
// 与 Python window_capture.py draw_frame 中的裁剪逻辑字节级一致：
//   - 当宽高比差异 <= 1% 时不裁剪（直接返回原图）
//   - 截图更宽时按 cfg.alignment 水平裁剪
//   - 截图更高时永远垂直居中裁剪
//
// 返回新分配的 *image.NRGBA；不修改 src。
func cropToAspectRatio(src *image.NRGBA, targetW, targetH int, alignment string) *image.NRGBA {
	sw, sh := src.Bounds().Dx(), src.Bounds().Dy()
	if sw <= 0 || sh <= 0 {
		return src
	}
	targetAspect := float64(targetW) / float64(targetH)
	shotAspect := float64(sw) / float64(sh)
	if absF(targetAspect-shotAspect) <= 0.01 {
		return src
	}

	var crop image.Rectangle
	if shotAspect > targetAspect {
		// 更宽 → 水平裁剪
		newW := int(float64(sh) * targetAspect)
		if newW < 1 {
			newW = 1
		}
		switch alignment {
		case "left":
			crop = image.Rect(0, 0, newW, sh)
		case "right":
			crop = image.Rect(sw-newW, 0, sw, sh)
		default: // center
			left := (sw - newW) / 2
			crop = image.Rect(left, 0, left+newW, sh)
		}
	} else {
		// 更高 → 垂直居中裁剪
		newH := int(float64(sw) / targetAspect)
		if newH < 1 {
			newH = 1
		}
		top := (sh - newH) / 2
		crop = image.Rect(0, top, sw, top+newH)
	}

	out := image.NewNRGBA(image.Rect(0, 0, crop.Dx(), crop.Dy()))
	for y := 0; y < crop.Dy(); y++ {
		si := src.PixOffset(crop.Min.X, crop.Min.Y+y)
		di := out.PixOffset(0, y)
		copy(out.Pix[di:di+4*crop.Dx()], src.Pix[si:si+4*crop.Dx()])
	}
	return out
}

func absF(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}
