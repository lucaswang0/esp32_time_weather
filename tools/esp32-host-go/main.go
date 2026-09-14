// esp32-host-go 入口（M2：启动 source + pipeline）。
// 完整的 GUI（Fyne）+ 系统托盘在 M5 接入。
package main

import (
	"context"
	"fmt"
	"image"
	"log/slog"
	"net/http"
	_ "net/http/pprof"
	"os"
	"os/signal"
	"path/filepath"

	"esp32_host/internal/config"
	"esp32_host/internal/gui"
	"esp32_host/internal/pipeline"
	"esp32_host/internal/source"
	"esp32_host/internal/transport"
)

const (
	exampleConfigRel = "config.example.yaml"
	userConfigRel    = "config.yaml"
)

func main() {
	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelInfo}))
	slog.SetDefault(logger)

	// 检查是否启用 GUI 模式（默认启用，除非设置了 HEADLESS=1 或传入 --cli 参数）
	useGUI := os.Getenv("HEADLESS") != "1"
	for _, arg := range os.Args[1:] {
		if arg == "--cli" || arg == "-cli" {
			useGUI = false
			break
		}
	}

	if err := run(logger, useGUI); err != nil {
		logger.Error("fatal", "err", err)
		os.Exit(1)
	}
}

func run(logger *slog.Logger, useGUI bool) error {
	cfgPath, err := resolveConfigPath()
	if err != nil {
		return fmt.Errorf("resolve config path: %w", err)
	}
	logger.Info("loading config", "path", cfgPath)

	cfg, err := config.Load(cfgPath)
	if err != nil {
		return fmt.Errorf("load config: %w", err)
	}

	logger.Info("config loaded",
		"global", cfg.Global,
		"pipelines", len(cfg.Pipelines),
	)

	if useGUI {
		// GUI 模式
		// pprof 诊断端点（仅本地），用于冻结时抓取 goroutine 堆栈
		go func() {
			_ = http.ListenAndServe("127.0.0.1:6060", nil)
		}()
		appState := gui.NewAppState(cfg, cfgPath, logger)
		// slog 日志同时转发到 GUI 日志视图（windowsgui 模式无控制台）
		tee := slog.New(gui.NewLogTee(logger.Handler(), appState))
		slog.SetDefault(tee)
		appState.SetLogger(tee)
		appState.Run()
		return nil
	}

	// CLI 模式（原有逻辑）
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, os.Kill)
	defer stop()

	discover := transport.NewDiscovery(cfg.Global.UDPBroadcastPort, logger)
	if err := discover.Start(); err != nil {
		logger.Warn("discovery start failed (continuing without auto-discovery)", "err", err)
	}
	defer discover.Close()

	pipes := make([]*pipeline.Pipeline, 0, len(cfg.Pipelines))
	for _, p := range cfg.Pipelines {
		if !p.Enabled {
			logger.Info("pipeline disabled, skipping", "pipeline", p.Name)
			continue
		}
		src, err := makeSource(p.Target, p.Source)
		if err != nil {
			logger.Error("make source", "pipeline", p.Name, "err", err)
			continue
		}
		pipe, err := pipeline.New(p, src, discover, logger)
		if err != nil {
			logger.Error("new pipeline", "pipeline", p.Name, "err", err)
			continue
		}
		if err := pipe.Start(ctx); err != nil {
			logger.Error("start pipeline", "pipeline", p.Name, "err", err)
			continue
		}
		pipes = append(pipes, pipe)
	}

	logger.Info("running",
		"active_pipelines", len(pipes),
		"hint", "press Ctrl+C to quit",
	)
	<-ctx.Done()
	logger.Info("shutdown signal received")

	// 串行 Stop，保证 wg 中并发逻辑可读
	for _, pipe := range pipes {
		pipe.Stop()
	}
	logger.Info("all pipelines stopped")
	return nil
}

// makeSource 根据 source.type 构造对应实现。
func makeSource(target config.Target, srcCfg config.Source) (source.Source, error) {
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

// resolveConfigPath 选择使用用户配置；若不存在，回落到 example。
func resolveConfigPath() (string, error) {
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		for _, rel := range []string{userConfigRel, exampleConfigRel} {
			p := filepath.Join(exeDir, rel)
			if _, err := os.Stat(p); err == nil {
				return p, nil
			}
		}
	}
	cwd, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for _, rel := range []string{userConfigRel, exampleConfigRel} {
		p := filepath.Join(cwd, rel)
		if _, err := os.Stat(p); err == nil {
			return p, nil
		}
	}
	return "", fmt.Errorf("no %s or %s found in exe-dir or cwd", userConfigRel, exampleConfigRel)
}
