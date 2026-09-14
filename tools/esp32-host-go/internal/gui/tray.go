//go:build !headless

package gui

import (
	"fmt"
	"log/slog"
	"sync"

	"fyne.io/fyne/v2"
	"fyne.io/systray"
)

// SystemTray wraps fyne.io/systray for system tray integration.
type SystemTray struct {
	fyneApp     fyne.App
	appState    *AppState
	ready       chan struct{}
	logger      *slog.Logger
	pipelineMux sync.Mutex
	pipelineItems map[string]*systray.MenuItem
}

// NewSystemTray creates a new system tray instance.
func NewSystemTray(fyneApp fyne.App, appState *AppState, logger *slog.Logger) *SystemTray {
	t := &SystemTray{
		fyneApp:       fyneApp,
		appState:      appState,
		ready:         make(chan struct{}),
		logger:        logger,
		pipelineItems: make(map[string]*systray.MenuItem),
	}
	return t
}

// Show shows the system tray icon and menu.
func (t *SystemTray) Show() {
	go func() {
		systray.Run(t.onReady, t.onExit)
	}()
	<-t.ready // 等待就绪
}

// Close closes the system tray.
func (t *SystemTray) Close() {
	systray.Quit()
}

// onReady is called when the system tray is ready.
func (t *SystemTray) onReady() {
	// 不设置图标，避免 PNG 解码问题
	systray.SetTitle("ESP32 Host Go")
	systray.SetTooltip("ESP32 Host Go - 正在运行")

	// 菜单项
	mShow := systray.AddMenuItem("显示主窗口", "显示主窗口")
	mHide := systray.AddMenuItem("隐藏到托盘", "隐藏主窗口到托盘")

	// 添加分隔线
	systray.AddSeparator()

	// Pipeline 菜单项（动态更新）
	t.updatePipelineMenuItems()

	systray.AddSeparator()

	mQuit := systray.AddMenuItem("退出", "退出程序")

	// 处理菜单点击
	go func() {
		for {
			select {
			case <-mShow.ClickedCh:
				fyne.Do(func() {
					t.appState.mainWin.Show()
				})
			case <-mHide.ClickedCh:
				fyne.Do(func() {
					t.appState.hideToTray()
				})
			case <-mQuit.ClickedCh:
				t.appState.Shutdown()
				t.appState.fyneApp.Quit()
				return
			}
		}
	}()

	close(t.ready)
	t.appState.logger.Debug("system tray ready")
}

// onExit is called when the system tray exits.
func (t *SystemTray) onExit() {
	t.appState.logger.Debug("system tray exited")
}

// UpdatePipelineStatus updates the tray tooltip with running pipeline count.
func (t *SystemTray) UpdatePipelineStatus(running int, total int) {
	tooltip := fmt.Sprintf("ESP32 Host Go - %d/%d pipelines running", running, total)
	systray.SetTooltip(tooltip)
}

// updatePipelineMenuItems updates the pipeline menu items in the tray.
func (t *SystemTray) updatePipelineMenuItems() {
	t.pipelineMux.Lock()
	defer t.pipelineMux.Unlock()

	t.appState.pipelineMux.RLock()
	pipelines := make(map[string]bool, len(t.appState.pipelines))
	for name, pl := range t.appState.pipelines {
		pipelines[name] = pl != nil
	}
	t.appState.pipelineMux.RUnlock()

	// 为每个 pipeline 创建菜单项
	for name, isRunning := range pipelines {
		if _, exists := t.pipelineItems[name]; !exists {
			item := systray.AddMenuItem(fmt.Sprintf("%s (%s)", name, statusText(isRunning)), fmt.Sprintf("Toggle pipeline %s", name))
			t.pipelineItems[name] = item

			// 处理点击
			go func(n string, mi *systray.MenuItem) {
				for range mi.ClickedCh {
					fyne.Do(func() {
						t.appState.pipelineMux.RLock()
						pl := t.appState.pipelines[n]
						t.appState.pipelineMux.RUnlock()
						if pl != nil {
							t.appState.stopPipeline(n)
						} else {
							t.appState.startPipeline(n)
						}
					})
				}
			}(name, item)
		} else {
			// 更新菜单项文本
			t.pipelineItems[name].SetTitle(fmt.Sprintf("%s (%s)", name, statusText(isRunning)))
		}
	}
}

// statusText returns status text for pipeline.
func statusText(running bool) string {
	if running {
		return "运行中"
	}
	return "已停止"
}