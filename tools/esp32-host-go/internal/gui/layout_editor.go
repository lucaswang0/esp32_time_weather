//go:build !headless

package gui

import (
	"fmt"
	"image/color"
	"math"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/widget"

	"esp32_host/internal/config"
)

// LayoutEditor 系统信息 widget 拖动布局编辑器。
// 以 1:1 比例显示 target 画布，每个 widget 一个可拖动矩形；
// 保存时把所有 widget 的显示坐标写回配置并持久化到 YAML。
type LayoutEditor struct {
	win     fyne.Window
	cfg     *config.PipelineConf
	persist func() error
	onSaved func()

	targetW, targetH int
	widgets          []config.SysInfoWidget
	boxes            []*draggableBox
	canvasArea       *fyne.Container
}

// ShowLayoutEditor 打开布局编辑器对话框。
// persist 在保存时被调用（写 YAML）；onSaved 在保存成功后回调（如重启 pipeline）。
func ShowLayoutEditor(win fyne.Window, cfg *config.PipelineConf, persist func() error, onSaved func()) {
	e := &LayoutEditor{win: win, cfg: cfg, persist: persist, onSaved: onSaved}
	e.targetW, e.targetH = cfg.Target.Width, cfg.Target.Height
	if e.targetW <= 0 {
		e.targetW = 320
	}
	if e.targetH <= 0 {
		e.targetH = 170
	}
	e.widgets = make([]config.SysInfoWidget, len(cfg.Source.SysInfo.Widgets))
	copy(e.widgets, cfg.Source.SysInfo.Widgets)

	e.canvasArea = container.NewWithoutLayout()
	e.rebuildBoxes()

	bg := canvas.NewRectangle(color.NRGBA{R: 24, G: 24, B: 24, A: 255})
	bg.SetMinSize(fyne.NewSize(float32(e.targetW), float32(e.targetH)))
	stage := container.NewStack(bg, e.canvasArea)

	saveBtn := widget.NewButton("保存布局", e.save)
	resetBtn := widget.NewButton("重置为默认布局", func() {
		for i := range e.widgets {
			e.widgets[i].X, e.widgets[i].Y = 0, 0
			e.widgets[i].W, e.widgets[i].H = 0, 0
		}
		e.rebuildBoxes()
	})
	content := container.NewVBox(
		widget.NewLabel(fmt.Sprintf("画布 %dx%d：拖动矩形调整位置，保存后写入 config.yaml", e.targetW, e.targetH)),
		container.NewCenter(stage),
		container.NewHBox(saveBtn, resetBtn),
	)
	dialog.ShowCustom("布局编辑器: "+cfg.Name, "关闭", content, win)
}

// rebuildBoxes 重建画布上的可拖动矩形。
// 有坐标的 widget 按配置坐标显示；无坐标的按栈式均分显示（与渲染器一致）。
func (e *LayoutEditor) rebuildBoxes() {
	e.canvasArea.RemoveAll()
	e.boxes = e.boxes[:0]

	stackCount := 0
	for _, w := range e.widgets {
		if w.W <= 0 || w.H <= 0 {
			stackCount++
		}
	}
	widgetH := float32(e.targetH) / float32(maxI(stackCount, 1))
	stackIdx := 0
	for i := range e.widgets {
		w := &e.widgets[i]
		var x, y, ww, hh float32
		if w.W > 0 && w.H > 0 {
			x, y, ww, hh = float32(w.X), float32(w.Y), float32(w.W), float32(w.H)
		} else {
			x, y, ww, hh = 0, float32(stackIdx)*widgetH, float32(e.targetW), widgetH
			stackIdx++
		}
		b := newDraggableBox(widgetTitle(w.Type), e)
		e.canvasArea.Objects = append(e.canvasArea.Objects, b)
		b.Move(fyne.NewPos(x, y))
		b.Resize(fyne.NewSize(ww, hh))
		b.title.SetText(fmt.Sprintf("%s (%d,%d)", b.baseTitle, int(math.Round(float64(x))), int(math.Round(float64(y)))))
		e.boxes = append(e.boxes, b)
	}
	e.canvasArea.Refresh()
}

// save 把各矩形的当前位置写回 widget 坐标，并调用 persist 持久化。
func (e *LayoutEditor) save() {
	for i, b := range e.boxes {
		w := &e.widgets[i]
		pos := b.Position()
		size := b.Size()
		w.X = int(math.Round(float64(pos.X)))
		w.Y = int(math.Round(float64(pos.Y)))
		w.W = int(math.Round(float64(size.Width)))
		w.H = int(math.Round(float64(size.Height)))
	}
	e.cfg.Source.SysInfo.Widgets = e.widgets
	if err := e.persist(); err != nil {
		dialog.ShowError(fmt.Errorf("保存配置失败: %w", err), e.win)
		return
	}
	if e.onSaved != nil {
		e.onSaved()
	}
	dialog.ShowInformation("布局编辑器", "布局已保存到配置文件", e.win)
}

// draggableBox 可拖动的 widget 预览矩形。
type draggableBox struct {
	widget.BaseWidget
	content   *fyne.Container
	rect      *canvas.Rectangle
	title     *widget.Label
	baseTitle string
	editor    *LayoutEditor
}

func newDraggableBox(title string, e *LayoutEditor) *draggableBox {
	b := &draggableBox{baseTitle: title, editor: e}
	b.rect = canvas.NewRectangle(color.NRGBA{R: 0x30, G: 0x60, B: 0xA0, A: 0x88})
	b.rect.StrokeWidth = 1
	b.rect.StrokeColor = color.NRGBA{R: 0x80, G: 0xC0, B: 0xFF, A: 255}
	b.title = widget.NewLabel(title)
	b.title.TextStyle = fyne.TextStyle{Monospace: true}
	b.content = container.NewStack(b.rect, container.NewCenter(b.title))
	b.ExtendBaseWidget(b)
	return b
}

func (b *draggableBox) CreateRenderer() fyne.WidgetRenderer {
	return widget.NewSimpleRenderer(b.content)
}

// Dragged 按 1:1 移动矩形，并限制在画布范围内。
func (b *draggableBox) Dragged(ev *fyne.DragEvent) {
	pos := b.Position()
	nx := clampF(pos.X+ev.Dragged.DX, 0, float32(b.editor.targetW)-b.Size().Width)
	ny := clampF(pos.Y+ev.Dragged.DY, 0, float32(b.editor.targetH)-b.Size().Height)
	b.Move(fyne.NewPos(nx, ny))
	b.title.SetText(fmt.Sprintf("%s (%d,%d)", b.baseTitle,
		int(math.Round(float64(nx))), int(math.Round(float64(ny)))))
}

func (b *draggableBox) DragEnd() {}

// Cursor 悬停时显示手型光标，提示可拖动。
func (b *draggableBox) Cursor() desktop.Cursor { return desktop.PointerCursor }

// widgetTitle 返回 widget 的默认标题（与渲染器 defaultWidgetTitle 一致）。
func widgetTitle(t string) string {
	switch t {
	case "cpu":
		return "CPU"
	case "mem":
		return "MEM"
	case "net":
		return "NET"
	case "disk":
		return "DISK"
	case "text":
		return "TEXT"
	}
	return t
}

func clampF(v, lo, hi float32) float32 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func maxI(a, b int) int {
	if a > b {
		return a
	}
	return b
}
