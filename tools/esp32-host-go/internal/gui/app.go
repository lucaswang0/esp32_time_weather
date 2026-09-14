//go:build !headless

package gui

import (
	"context"
	"fmt"
	"image"
	"log/slog"
	"os"
	"os/signal"
	"strings"
	"sync"
	"time"

	"esp32_host/internal/config"
	"esp32_host/internal/pipeline"
	"esp32_host/internal/source"
	"esp32_host/internal/transport"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

// AppState holds the GUI application state.
type AppState struct {
	fyneApp      fyne.App
	mainWin      fyne.Window
	logger       *slog.Logger
	config       *config.Config
	configPath   string
	pipelines    map[string]*pipeline.Pipeline
	pipelineMux  sync.RWMutex
	ctx          context.Context
	cancel       context.CancelFunc
	discover     *transport.Discovery
	logLines     []string
	logMux       sync.Mutex
	logFlushed   int // 已刷新到 UI 的行数（受 logMux 保护）
	logScroll    *container.Scroll
	logContent   *widget.Label
	pipelineList *widget.List
	statusBar    *widget.Label
	selectedIdx  *int // selected pipeline index
	configForm   *widget.Form
}

// NewAppState creates a new GUI application state.
func NewAppState(cfg *config.Config, configPath string, logger *slog.Logger) *AppState {
	ctx, cancel := context.WithCancel(context.Background())
	fyneApp := app.NewWithID("com.esp32.host.gui")
	// 不设置窗口图标，避免 PNG 解码问题

	mainWin := fyneApp.NewWindow("ESP32 Host Go")
	mainWin.Resize(fyne.NewSize(1000, 700))
	mainWin.CenterOnScreen()

	state := &AppState{
		fyneApp:    fyneApp,
		mainWin:    mainWin,
		logger:     logger,
		config:     cfg,
		configPath: configPath,
		pipelines:  make(map[string]*pipeline.Pipeline),
		ctx:        ctx,
		cancel:     cancel,
		logLines:   make([]string, 0, 500),
	}
	// 构建 UI
	state.buildUI()

	return state
}

// buildUI constructs the main window layout.
func (s *AppState) buildUI() {
	// 左侧：Pipeline 列表
	s.pipelineList = widget.NewList(
		func() int {
			s.pipelineMux.RLock()
			defer s.pipelineMux.RUnlock()
			return len(s.config.Pipelines)
		},
		func() fyne.CanvasObject {
			return container.NewHBox(
				widget.NewIcon(nil), // 状态图标占位
				widget.NewLabel("Pipeline"),
				widget.NewButton("▶", nil), // 启停按钮
			)
		},
		func(id widget.ListItemID, obj fyne.CanvasObject) {
			s.pipelineMux.RLock()
			defer s.pipelineMux.RUnlock()
			if id >= len(s.config.Pipelines) {
				return
			}
			p := s.config.Pipelines[id]
			hbox := obj.(*fyne.Container)
			icon := hbox.Objects[0].(*widget.Icon)
			nameLabel := hbox.Objects[1].(*widget.Label)
			btn := hbox.Objects[2].(*widget.Button)

			nameLabel.SetText(p.Name)
			if p.Enabled {
				icon.SetResource(theme.MediaPlayIcon()) // 运行中
				btn.SetText("■")
				btn.OnTapped = func() { s.stopPipeline(p.Name) }
			} else {
				icon.SetResource(theme.MediaPauseIcon()) // 停止
				btn.SetText("▶")
				btn.OnTapped = func() { s.startPipeline(p.Name) }
			}
		},
	)

	// 选中回调
	s.pipelineList.OnSelected = func(id widget.ListItemID) {
		s.showPipelineConfig(id)
	}

	// 右侧：配置表单 / 日志
	s.logContent = widget.NewLabel("")
	s.logContent.Wrapping = fyne.TextWrapOff
	s.logScroll = container.NewScroll(s.logContent)

	// 初始配置详情卡片
	configCard := widget.NewCard("配置详情", "", widget.NewLabel("选择左侧 Pipeline 查看/编辑配置"))

	// 底部状态栏
	s.statusBar = widget.NewLabel("就绪 | 0 个 Pipeline 运行中")

	// 布局：左侧列表 + 右侧内容
	tabs := container.NewTabContainer(
		container.NewTabItem("配置", configCard),
		container.NewTabItem("日志", s.logScroll),
	)
	rightPanel := tabs

	// Pipeline list with toolbar at top
	pipelineToolbar := container.NewBorder(
		widget.NewLabel("Pipelines"),
		container.NewHBox(
			widget.NewButton("+ 添加", s.showAddPipelineDialog),
			widget.NewButton("保存配置", s.saveConfig),
		),
		nil, nil,
		s.pipelineList,
	)

	split := container.NewHSplit(
		pipelineToolbar,
		rightPanel,
	)
	split.SetOffset(0.3)

	mainContent := container.NewBorder(
		nil,
		s.statusBar,
		nil, nil,
		split,
	)

	s.mainWin.SetContent(mainContent)

	// 保存 configCard 引用以便更新
	s.configForm = nil
}

// Run starts the GUI application.
func (s *AppState) Run() {
	// 显示主窗口（必须在托盘之前，确保窗口已创建）
	s.mainWin.Show()
	s.logger.Info("main window shown")

	// 启动 UDP 发现
	discover := transport.NewDiscovery(s.config.Global.UDPBroadcastPort, s.logger)
	if err := discover.Start(); err != nil {
		s.logger.Warn("discovery start failed", "err", err)
	}
	s.discover = discover

	// 自动启动已启用的 pipeline
	for _, p := range s.config.Pipelines {
		if p.Enabled {
			s.startPipeline(p.Name)
		}
	}

	// 窗口关闭 = 退出程序（停止所有 pipeline 后退出）
	s.mainWin.SetCloseIntercept(func() {
		s.appendLog("窗口关闭，正在退出...")
		s.Shutdown()
		s.fyneApp.Quit()
	})

	// 信号处理
	sigCtx, stop := signal.NotifyContext(s.ctx, os.Interrupt, os.Kill)
	defer stop()

	go func() {
		<-sigCtx.Done()
		s.logger.Info("shutdown signal received")
		s.cancel()
		s.fyneApp.Quit()
	}()

	// 定期刷新 UI
	go s.uiRefreshLoop()

	// 定期输出 metrics 到日志
	go s.metricsRefreshLoop()

	s.fyneApp.Run()
}

// uiRefreshLoop periodically updates the UI (pipeline list, status bar).
func (s *AppState) uiRefreshLoop() {
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-s.ctx.Done():
			return
		case <-ticker.C:
			fyne.Do(func() {
				s.refreshPipelineList()
				s.updateStatusBar()
				s.flushLogs()
			})
		}
	}
}

// refreshPipelineList refreshes the pipeline list widget.
func (s *AppState) refreshPipelineList() {
	if s.pipelineList != nil {
		s.pipelineList.Refresh()
	}
}

// updateStatusBar updates the status bar text.
func (s *AppState) updateStatusBar() {
	s.pipelineMux.RLock()
	running := 0
	for _, pl := range s.pipelines {
		if pl != nil {
			running++
		}
	}
	s.pipelineMux.RUnlock()
	if s.statusBar != nil {
		s.statusBar.SetText(fmt.Sprintf("就绪 | %d 个 Pipeline 运行中", running))
	}
}

// startPipeline starts a pipeline by name.
func (s *AppState) startPipeline(name string) {
	s.pipelineMux.Lock()

	if _, exists := s.pipelines[name]; exists {
		s.pipelineMux.Unlock()
		s.logger.Info("pipeline already running", "name", name)
		return
	}

	// 找到配置
	var pipeCfg *config.PipelineConf
	for i := range s.config.Pipelines {
		if s.config.Pipelines[i].Name == name {
			pipeCfg = &s.config.Pipelines[i]
			break
		}
	}
	if pipeCfg == nil {
		s.pipelineMux.Unlock()
		s.logger.Error("pipeline config not found", "name", name)
		return
	}

	// 构造 source
	src, err := s.makeSource(pipeCfg.Target, pipeCfg.Source)
	if err != nil {
		s.pipelineMux.Unlock()
		s.logger.Error("make source failed", "name", name, "err", err)
		dialog.ShowError(err, s.mainWin)
		return
	}

	// 创建并启动 pipeline
	pl, err := pipeline.New(*pipeCfg, src, s.discover, s.logger.With("pipeline", name))
	if err != nil {
		s.pipelineMux.Unlock()
		s.logger.Error("pipeline.New failed", "name", name, "err", err)
		dialog.ShowError(err, s.mainWin)
		return
	}

	go pl.Start(s.ctx)

	s.pipelines[name] = pl
	s.pipelineMux.Unlock() // 必须先释放写锁再刷新 UI：List.Refresh 会触发 Length 回调请求 RLock，锁内调用会自死锁

	s.logger.Info("pipeline started", "name", name)
	s.appendLog(fmt.Sprintf("[%s] started", name))
	s.pipelineList.Refresh()
}

// stopPipeline stops a pipeline by name.
func (s *AppState) stopPipeline(name string) {
	s.pipelineMux.Lock()

	pl, ok := s.pipelines[name]
	if !ok {
		s.pipelineMux.Unlock()
		return
	}
	pl.Stop()
	delete(s.pipelines, name)
	s.pipelineMux.Unlock() // 必须先释放写锁再刷新 UI（同 startPipeline）

	s.logger.Info("pipeline stopped", "name", name)
	s.appendLog(fmt.Sprintf("[%s] stopped", name))
	s.pipelineList.Refresh()
}

// makeSource creates a source based on configuration.
func (s *AppState) makeSource(target config.Target, srcCfg config.Source) (source.Source, error) {
	switch srcCfg.Type {
	case "screen":
		var region *image.Rectangle
		if srcCfg.Screen.Region != nil {
			r := image.Rect(
				srcCfg.Screen.Region.Left,
				srcCfg.Screen.Region.Top,
				srcCfg.Screen.Region.Left+srcCfg.Screen.Region.Width,
				srcCfg.Screen.Region.Top+srcCfg.Screen.Region.Height,
			)
			region = &r
		}
		return source.NewScreenSource(srcCfg.Screen.Monitor, region), nil
	case "window":
		return source.NewWindowSource(srcCfg.Window, target.Width, target.Height), nil
	case "sysinfo":
		return source.NewSysInfoSource(srcCfg.SysInfo, target.Width, target.Height), nil
	default:
		return nil, fmt.Errorf("unsupported source.type: %q", srcCfg.Type)
	}
}

// showAddPipelineDialog shows dialog to add a new pipeline.
func (s *AppState) showAddPipelineDialog() {
	nameEntry := widget.NewEntry()
	nameEntry.SetPlaceHolder("Pipeline 名称")

	typeSelect := widget.NewSelect([]string{"screen", "window", "sysinfo"}, nil)
	typeSelect.SetSelected("screen")

	form := &widget.Form{
		Items: []*widget.FormItem{
			{Text: "名称", Widget: nameEntry},
			{Text: "数据源类型", Widget: typeSelect},
		},
		OnSubmit: func() {
			name := nameEntry.Text
			if name == "" {
				dialog.ShowError(fmt.Errorf("名称不能为空"), s.mainWin)
				return
			}
			// 检查重名
			for _, p := range s.config.Pipelines {
				if p.Name == name {
					dialog.ShowError(fmt.Errorf("名称已存在"), s.mainWin)
					return
				}
			}
			// 添加到配置
			s.config.Pipelines = append(s.config.Pipelines, config.PipelineConf{
				Name:    name,
				Enabled: false,
				ESP32: config.ESP32{
					Host:              "",
					Port:              8888,
					UseBroadcast:      true,
					BroadcastHoldTime: 30,
					SocketTimeoutSec:  2.0,
				},
				Target: config.Target{Width: 170, Height: 320},
				Source: config.Source{
					Type:   typeSelect.Selected,
					Screen: config.ScreenOpts{Monitor: 0},
					Window: config.WindowOpts{Title: "", CropAlignment: "center"},
					SysInfo: config.SysInfoOpts{
						RefreshMs: 1000,
						Widgets: []config.SysInfoWidget{
							{Type: "text", Text: "STATUS"},
							{Type: "cpu"},
							{Type: "mem"},
							{Type: "net"},
							{Type: "disk"},
						},
					},
				},
				Diff: config.Diff{
					TargetFPS:     24.0,
					MinThreshold:  1,
					MaxThreshold:  180,
					StepUp:        15,
					StepDown:      5,
					Hysteresis:    0.03,
					GeneratorRate: 0.033,
				},
				Colors: config.Colors{
					Gamma:   1.2,
					WBScale: [3]float64{1.1, 1.05, 0.95},
				},
				Queue: config.Queue{
					MaxSize:      8,
					LowWaterMark: 3,
				},
				Logging: config.Logging{
					Level: "info",
				},
			})
			s.logger.Info("pipeline added", "name", name)
			s.appendLog(fmt.Sprintf("添加 pipeline: %s", name))
			s.pipelineList.Refresh()
		},
	}

	dialog.ShowCustom("添加 Pipeline", "取消", form, s.mainWin)
}

// showPipelineConfig shows the configuration form for the selected pipeline.
func (s *AppState) showPipelineConfig(id widget.ListItemID) {
	s.pipelineMux.RLock()
	defer s.pipelineMux.RUnlock()

	if int(id) >= len(s.config.Pipelines) {
		return
	}

	p := &s.config.Pipelines[id]
	idx := int(id)
	s.selectedIdx = &idx

	// Create form items
	nameEntry := widget.NewEntry()
	nameEntry.SetText(p.Name)
	nameEntry.OnChanged = func(text string) {
		p.Name = text
	}

	enabledCheck := widget.NewCheck("启用", func(checked bool) {
		p.Enabled = checked
	})
	enabledCheck.SetChecked(p.Enabled)

	hostEntry := widget.NewEntry()
	hostEntry.SetText(p.ESP32.Host)
	hostEntry.SetPlaceHolder("留空则使用广播发现")
	hostEntry.OnChanged = func(text string) {
		p.ESP32.Host = text
	}

	portEntry := widget.NewEntry()
	portEntry.SetText(fmt.Sprintf("%d", p.ESP32.Port))
	portEntry.OnChanged = func(text string) {
		var val int
		fmt.Sscanf(text, "%d", &val)
		if val > 0 {
			p.ESP32.Port = val
		}
	}

	useBroadcastCheck := widget.NewCheck("使用广播", func(checked bool) {
		p.ESP32.UseBroadcast = checked
	})
	useBroadcastCheck.SetChecked(p.ESP32.UseBroadcast)

	targetWidthEntry := widget.NewEntry()
	targetWidthEntry.SetText(fmt.Sprintf("%d", p.Target.Width))
	targetWidthEntry.OnChanged = func(text string) {
		var val int
		fmt.Sscanf(text, "%d", &val)
		if val > 0 {
			p.Target.Width = val
		}
	}

	targetHeightEntry := widget.NewEntry()
	targetHeightEntry.SetText(fmt.Sprintf("%d", p.Target.Height))
	targetHeightEntry.OnChanged = func(text string) {
		var val int
		fmt.Sscanf(text, "%d", &val)
		if val > 0 {
			p.Target.Height = val
		}
	}

	sourceTypeSelect := widget.NewSelect([]string{"screen", "window", "sysinfo"}, func(selected string) {
		p.Source.Type = selected
	})
	sourceTypeSelect.SetSelected(p.Source.Type)

	monitorEntry := widget.NewEntry()
	monitorEntry.SetText(fmt.Sprintf("%d", p.Source.Screen.Monitor))
	monitorEntry.OnChanged = func(text string) {
		var val int
		fmt.Sscanf(text, "%d", &val)
		p.Source.Screen.Monitor = val
	}

	windowTitleEntry := widget.NewEntry()
	windowTitleEntry.SetText(p.Source.Window.Title)
	windowTitleEntry.OnChanged = func(text string) {
		p.Source.Window.Title = text
	}

	cropAlignSelect := widget.NewSelect([]string{"left", "center", "right"}, func(selected string) {
		p.Source.Window.CropAlignment = selected
	})
	cropAlignSelect.SetSelected(p.Source.Window.CropAlignment)

	sysinfoRefreshEntry := widget.NewEntry()
	sysinfoRefreshEntry.SetText(fmt.Sprintf("%d", p.Source.SysInfo.RefreshMs))
	sysinfoRefreshEntry.OnChanged = func(text string) {
		var val int
		fmt.Sscanf(text, "%d", &val)
		if val > 0 {
			p.Source.SysInfo.RefreshMs = val
		}
	}

	diffFPSEntry := widget.NewEntry()
	diffFPSEntry.SetText(fmt.Sprintf("%.1f", p.Diff.TargetFPS))
	diffFPSEntry.OnChanged = func(text string) {
		var val float64
		fmt.Sscanf(text, "%f", &val)
		if val > 0 {
			p.Diff.TargetFPS = val
		}
	}

	form := &widget.Form{
		Items: []*widget.FormItem{
			{Text: "名称", Widget: nameEntry},
			{Text: "启用", Widget: enabledCheck},
			{Text: "ESP32 Host", Widget: hostEntry},
			{Text: "ESP32 Port", Widget: portEntry},
			{Text: "使用广播", Widget: useBroadcastCheck},
			{Text: "目标宽度", Widget: targetWidthEntry},
			{Text: "目标高度", Widget: targetHeightEntry},
			{Text: "数据源类型", Widget: sourceTypeSelect},
			{Text: "显示器编号", Widget: monitorEntry},
			{Text: "窗口标题", Widget: windowTitleEntry},
			{Text: "裁剪对齐", Widget: cropAlignSelect},
			{Text: "SysInfo刷新(ms)", Widget: sysinfoRefreshEntry},
			{Text: "目标FPS", Widget: diffFPSEntry},
		},
		OnSubmit: func() {
			s.appendLog(fmt.Sprintf("更新 pipeline 配置: %s", p.Name))
			s.pipelineList.Refresh()
		},
	}

	// sysinfo 数据源提供拖动布局编辑器入口
	layoutBtn := widget.NewButton("布局编辑器…", func() {
		if p.Source.Type != "sysinfo" {
			dialog.ShowInformation("提示", "布局编辑器仅支持 sysinfo 数据源", s.mainWin)
			return
		}
		s.showLayoutEditor(p)
	})
	form.Items = append(form.Items, &widget.FormItem{Text: "布局", Widget: layoutBtn})

	// 更新右侧配置卡片 - 这里通过 dialog 显示，后续可改为更新右侧面板
	dialog.ShowCustom(fmt.Sprintf("配置: %s", p.Name), "关闭", form, s.mainWin)
	s.appendLog(fmt.Sprintf("选中 pipeline: %s", p.Name))
}

// saveConfig saves the current configuration to file.
func (s *AppState) saveConfig() {
	if err := s.persistConfig(); err != nil {
		s.appendLog("保存配置失败: " + err.Error())
		dialog.ShowError(err, s.mainWin)
		return
	}
	s.appendLog("配置已保存: " + s.configPath)
}

// persistConfig writes the in-memory config back to the YAML file.
func (s *AppState) persistConfig() error {
	if s.configPath == "" {
		return fmt.Errorf("配置文件路径未知")
	}
	return s.config.Save(s.configPath)
}

// showLayoutEditor opens the draggable layout editor for a sysinfo pipeline.
func (s *AppState) showLayoutEditor(p *config.PipelineConf) {
	ShowLayoutEditor(s.mainWin, p, s.persistConfig, func() {
		// 运行中的 pipeline 重启以应用新布局
		s.pipelineMux.RLock()
		_, running := s.pipelines[p.Name]
		s.pipelineMux.RUnlock()
		if running {
			s.stopPipeline(p.Name)
			s.startPipeline(p.Name)
			s.appendLog("布局已更新，pipeline 已重启: " + p.Name)
		} else {
			s.appendLog("布局已更新: " + p.Name)
		}
	})
}

// appendLog appends a log line to the log buffer.
// 线程安全：仅写数据，UI 更新由 uiRefreshLoop 在主线程统一刷新。
func (s *AppState) appendLog(line string) {
	s.logMux.Lock()
	defer s.logMux.Unlock()
	s.logLines = append(s.logLines, time.Now().Format("15:04:05")+" "+line)
	if len(s.logLines) > 500 {
		s.logLines = s.logLines[len(s.logLines)-500:]
		s.logFlushed = 0 // 触发全量重建
	}
}

// flushLogs pushes pending log lines to the log view.
// 必须在 UI 主线程调用（fyne.Do 内部）。
func (s *AppState) flushLogs() {
	s.logMux.Lock()
	if s.logFlushed >= len(s.logLines) {
		s.logMux.Unlock()
		return
	}
	text := strings.Join(s.logLines, "\n")
	s.logFlushed = len(s.logLines)
	s.logMux.Unlock()

	s.logContent.SetText(text)
	s.logScroll.ScrollToBottom()
}

// hideToTray hides the main window.
func (s *AppState) hideToTray() {
	s.mainWin.Hide()
	s.appendLog("窗口已隐藏")
}

// showWindow shows the main window.
func (s *AppState) showWindow() {
	s.mainWin.Show()
}

// Shutdown gracefully shuts down the application.
func (s *AppState) Shutdown() {
	s.cancel()
	for name := range s.pipelines {
		s.stopPipeline(name)
	}
	if s.discover != nil {
		s.discover.Close()
	}
}

// metricsRefreshLoop periodically updates log with pipeline metrics.
func (s *AppState) metricsRefreshLoop() {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-s.ctx.Done():
			return
		case <-ticker.C:
			s.pipelineMux.RLock()
			for name, pl := range s.pipelines {
				if pl != nil {
					m := pl.Metrics()
					fps := m.FPS()
					framesGenerated := m.FramesGenerated.Load()
					framesProcessed := m.FramesProcessed.Load()
					chunksSent := m.ChunksSent.Load()
					reconnections := m.Reconnections.Load()
					connErrors := m.ConnectionErrors.Load()
					sendErrors := m.SendErrors.Load()
					line := fmt.Sprintf("[%s] fps=%.1f gen=%d proc=%d chunks=%d recon=%d c_err=%d s_err=%d",
						name, fps, framesGenerated, framesProcessed, chunksSent, reconnections, connErrors, sendErrors)
					s.logger.Info("pipeline metrics", "metrics", line)
					s.appendLog(line)
				}
			}
			s.pipelineMux.RUnlock()
		}
	}
}

// SetLogger replaces the app logger (used to inject the GUI log tee).
func (s *AppState) SetLogger(l *slog.Logger) { s.logger = l }

// NewLogTee wraps a slog.Handler so records also appear in the GUI log view.
func NewLogTee(inner slog.Handler, app *AppState) slog.Handler {
	return &logTeeHandler{inner: inner, app: app}
}

// logTeeHandler tees slog records to the GUI log buffer and the inner handler.
type logTeeHandler struct {
	inner slog.Handler
	app   *AppState
}

func (h *logTeeHandler) Enabled(ctx context.Context, l slog.Level) bool {
	return h.inner.Enabled(ctx, l)
}

func (h *logTeeHandler) Handle(ctx context.Context, r slog.Record) error {
	var b strings.Builder
	b.WriteString(r.Time.Format("15:04:05"))
	b.WriteString(" [")
	b.WriteString(r.Level.String())
	b.WriteString("] ")
	b.WriteString(r.Message)
	r.Attrs(func(a slog.Attr) bool {
		b.WriteString(" ")
		b.WriteString(a.Key)
		b.WriteString("=")
		b.WriteString(a.Value.String())
		return true
	})
	h.app.appendLog(b.String())
	return h.inner.Handle(ctx, r)
}

func (h *logTeeHandler) WithAttrs(attrs []slog.Attr) slog.Handler {
	return &logTeeHandler{inner: h.inner.WithAttrs(attrs), app: h.app}
}

func (h *logTeeHandler) WithGroup(name string) slog.Handler {
	return &logTeeHandler{inner: h.inner.WithGroup(name), app: h.app}
}
