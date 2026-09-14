// Package config 定义 YAML 配置结构与加载逻辑。
//
// 配置由两部分组成：Global (全局默认值) 和 Pipelines (一个或多个 pipeline)。
// 缺失的可选字段会用 Global 中同名字段或内置默认值补齐。
package config

import (
	"fmt"
	"os"
	"time"

	"gopkg.in/yaml.v3"
)

// Config 是整个 YAML 文档对应的根结构。
type Config struct {
	Global    Global         `yaml:"global"`
	Pipelines []PipelineConf `yaml:"pipelines"`
}

// Global 全局默认配置，pipeline 未显式指定时使用。
type Global struct {
	LogLevel             string  `yaml:"log_level"`
	UDPBroadcastPort     int     `yaml:"udp_broadcast_port"`
	ReconnectIntervalSec float64 `yaml:"reconnect_interval_sec"`
	HeartbeatIntervalSec float64 `yaml:"heartbeat_interval_sec"`
	MaxChunkDataSize     int     `yaml:"max_chunk_data_size"`
	FramesQueueMaxSize   int     `yaml:"frames_queue_max_size"`
	GeneratorLowWater    int     `yaml:"generator_low_water_mark"`
	FPSHistorySize       int     `yaml:"fps_history_size"`
}

// PipelineConf 描述单个 pipeline 的全部参数。
type PipelineConf struct {
	Name    string  `yaml:"name"`
	Enabled bool    `yaml:"enabled"`
	ESP32   ESP32   `yaml:"esp32"`
	Target  Target  `yaml:"target"`
	Source  Source  `yaml:"source"`
	Diff    Diff    `yaml:"diff"`
	Colors  Colors  `yaml:"colors"`
	Queue   Queue   `yaml:"queue"`
	Logging Logging `yaml:"logging"`
}

// ESP32 控制如何定位并连接 ESP32。
//   - Host 为空时启用广播发现
//   - UseBroadcast 控制是否监听 UDP 广播覆盖 Host
type ESP32 struct {
	Host              string  `yaml:"host"`
	Port              int     `yaml:"port"`
	UseBroadcast      bool    `yaml:"use_broadcast"`
	BroadcastHoldTime float64 `yaml:"broadcast_hold_time"`
	SocketTimeoutSec  float64 `yaml:"socket_timeout_sec"`
}

// Target 描述目标 TFT 屏幕像素尺寸。
type Target struct {
	Width  int `yaml:"width"`
	Height int `yaml:"height"`
}

// Source 描述帧来源；按 Type 路由到 screen / window / sysinfo 实现（M2+ 接入）。
type Source struct {
	Type    string      `yaml:"type"` // screen | window | sysinfo
	Screen  ScreenOpts  `yaml:"screen"`
	Window  WindowOpts  `yaml:"window"`
	SysInfo SysInfoOpts `yaml:"sysinfo"`
}

// ScreenOpts 桌面截屏参数。
type ScreenOpts struct {
	Monitor int     `yaml:"monitor"`
	Region  *Region `yaml:"region,omitempty"`
}

// Region 子区域截屏（未指定则整屏）。
type Region struct {
	Top    int `yaml:"top"`
	Left   int `yaml:"left"`
	Width  int `yaml:"width"`
	Height int `yaml:"height"`
}

// WindowOpts 窗口截屏参数。
type WindowOpts struct {
	Title         string `yaml:"title"`
	CropAlignment string `yaml:"crop_alignment"` // left|center|right
}

// SysInfoOpts 系统性能仪表盘参数。
// RefreshMs 默认 1000；Widgets 为空时渲染 默认布局（CPU+MEM+NET+DISK 4 条）。
type SysInfoOpts struct {
	RefreshMs int            `yaml:"refresh_ms"`
	Widgets   []SysInfoWidget `yaml:"widgets"`
}

// SysInfoWidget 单个仪表盘条目。
// 默认（x/y/w/h 全为 0）按数组顺序从上到下均分渲染；
// w/h 均大于 0 时按绝对坐标渲染（坐标单位为 pipeline target 画布像素）。
type SysInfoWidget struct {
	Type string `yaml:"type"`           // cpu | mem | net | disk | text
	Text string `yaml:"text,omitempty"` // type=text 时的内容
	Name string `yaml:"name,omitempty"` // 自定义标题（默认根据 type 自动生成）
	X    int    `yaml:"x,omitempty"`    // 绝对布局左上角
	Y    int    `yaml:"y,omitempty"`
	W    int    `yaml:"w,omitempty"` // >0 时启用绝对布局
	H    int    `yaml:"h,omitempty"`
}

// Diff 自适应阈值相关参数。
type Diff struct {
	TargetFPS     float64 `yaml:"target_fps"`
	MinThreshold  int     `yaml:"min_threshold"`
	MaxThreshold  int     `yaml:"max_threshold"`
	StepUp        int     `yaml:"step_up"`
	StepDown      int     `yaml:"step_down"`
	Hysteresis    float64 `yaml:"hysteresis"`
	GeneratorRate float64 `yaml:"generator_target_interval_sec"`
}

// Colors 颜色校正（gamma + 白平衡）。
type Colors struct {
	Gamma   float64  `yaml:"gamma"`
	WBScale [3]float64 `yaml:"wb_scale"`
}

// Queue 帧队列配置。
type Queue struct {
	MaxSize       int `yaml:"max_size"`
	LowWaterMark  int `yaml:"low_water_mark"`
}

// Logging 单个 pipeline 的日志偏好。
type Logging struct {
	Level string `yaml:"level"`
}

// Load 从 path 读取 YAML 并应用默认值。
func Load(path string) (*Config, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	cfg := &Config{}
	if err := yaml.Unmarshal(raw, cfg); err != nil {
		return nil, fmt.Errorf("parse %s: %w", path, err)
	}
	cfg.applyDefaults()
	if err := cfg.validate(); err != nil {
		return nil, err
	}
	return cfg, nil
}

// Save 将配置写回 YAML 文件（用于 GUI 布局编辑器等场景）。
func (c *Config) Save(path string) error {
	raw, err := yaml.Marshal(c)
	if err != nil {
		return fmt.Errorf("marshal config: %w", err)
	}
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		return fmt.Errorf("write %s: %w", path, err)
	}
	return nil
}

// applyDefaults 填充所有缺失字段为合理默认值。
func (c *Config) applyDefaults() {
	// Global 默认值
	if c.Global.LogLevel == "" {
		c.Global.LogLevel = "info"
	}
	if c.Global.UDPBroadcastPort == 0 {
		c.Global.UDPBroadcastPort = 8889
	}
	if c.Global.ReconnectIntervalSec == 0 {
		c.Global.ReconnectIntervalSec = 3.0
	}
	if c.Global.HeartbeatIntervalSec == 0 {
		c.Global.HeartbeatIntervalSec = 2.0
	}
	if c.Global.MaxChunkDataSize == 0 {
		c.Global.MaxChunkDataSize = 8192
	}
	if c.Global.FramesQueueMaxSize == 0 {
		c.Global.FramesQueueMaxSize = 8
	}
	if c.Global.GeneratorLowWater == 0 {
		c.Global.GeneratorLowWater = 3
	}
	if c.Global.FPSHistorySize == 0 {
		c.Global.FPSHistorySize = 3
	}

	// Pipeline 默认值
	for i := range c.Pipelines {
		p := &c.Pipelines[i]
		if p.ESP32.Port == 0 {
			p.ESP32.Port = 8888
		}
		if p.ESP32.SocketTimeoutSec == 0 {
			p.ESP32.SocketTimeoutSec = 2.0
		}
		if p.ESP32.BroadcastHoldTime == 0 {
			p.ESP32.BroadcastHoldTime = 30.0
		}
		if p.Target.Width == 0 {
			p.Target.Width = 170
		}
		if p.Target.Height == 0 {
			p.Target.Height = 320
		}
		if p.Source.Type == "" {
			p.Source.Type = "screen"
		}
		if p.Source.Window.CropAlignment == "" {
			p.Source.Window.CropAlignment = "center"
		}
		// SysInfo 默认值
		if p.Source.SysInfo.RefreshMs == 0 {
			p.Source.SysInfo.RefreshMs = 1000
		}
		if len(p.Source.SysInfo.Widgets) == 0 {
			p.Source.SysInfo.Widgets = []SysInfoWidget{
				{Type: "cpu"},
				{Type: "mem"},
				{Type: "net"},
				{Type: "disk"},
			}
		}
		if p.Diff.TargetFPS == 0 {
			p.Diff.TargetFPS = 24.0
		}
		if p.Diff.MinThreshold == 0 {
			p.Diff.MinThreshold = 1
		}
		if p.Diff.MaxThreshold == 0 {
			p.Diff.MaxThreshold = 180
		}
		if p.Diff.StepUp == 0 {
			p.Diff.StepUp = 15
		}
		if p.Diff.StepDown == 0 {
			p.Diff.StepDown = 5
		}
		if p.Diff.Hysteresis == 0 {
			p.Diff.Hysteresis = 0.03
		}
		if p.Diff.GeneratorRate == 0 {
			p.Diff.GeneratorRate = 0.033
		}
		if p.Colors.Gamma == 0 {
			p.Colors.Gamma = 1.0
		}
		if p.Colors.WBScale == [3]float64{} {
			p.Colors.WBScale = [3]float64{1.0, 1.0, 1.0}
		}
		if p.Queue.MaxSize == 0 {
			p.Queue.MaxSize = c.Global.FramesQueueMaxSize
		}
		if p.Queue.LowWaterMark == 0 {
			p.Queue.LowWaterMark = c.Global.GeneratorLowWater
		}
		if p.Logging.Level == "" {
			p.Logging.Level = c.Global.LogLevel
		}
	}
}

// validate 检查必填字段与数值范围。
func (c *Config) validate() error {
	if len(c.Pipelines) == 0 {
		return fmt.Errorf("at least one pipeline required")
	}
	for i, p := range c.Pipelines {
		if p.Name == "" {
			return fmt.Errorf("pipelines[%d]: name is required", i)
		}
		if p.Target.Width <= 0 || p.Target.Height <= 0 {
			return fmt.Errorf("pipeline %q: target.width/height must be > 0", p.Name)
		}
		switch p.Source.Type {
		case "screen", "window", "sysinfo":
		default:
			return fmt.Errorf("pipeline %q: unknown source.type %q (want screen|window|sysinfo)", p.Name, p.Source.Type)
		}
		if p.Diff.MinThreshold < 0 || p.Diff.MinThreshold > p.Diff.MaxThreshold {
			return fmt.Errorf("pipeline %q: diff.min_threshold (%d) invalid (max=%d)",
				p.Name, p.Diff.MinThreshold, p.Diff.MaxThreshold)
		}
		if p.Colors.Gamma <= 0 {
			return fmt.Errorf("pipeline %q: colors.gamma must be > 0", p.Name)
		}
	}
	return nil
}

// SocketTimeout 单次拨号超时。
func (p *PipelineConf) SocketTimeout() time.Duration {
	return secondsToDuration(p.ESP32.SocketTimeoutSec)
}

// BroadcastHold broadcast IP 的有效窗口。
func (p *PipelineConf) BroadcastHold() time.Duration {
	return secondsToDuration(p.ESP32.BroadcastHoldTime)
}

// ReconnectInterval 连接失败后等待多久再重试。
func (p *PipelineConf) ReconnectInterval() time.Duration {
	if p.Queue.MaxSize == 0 {
		return 3 * time.Second
	}
	return 3 * time.Second // 与 global.reconnect_interval_sec 对齐的固定兜底
}

// HeartbeatInterval 心跳发送间隔。
func (p *PipelineConf) HeartbeatInterval() time.Duration {
	return 2 * time.Second // 与 global.heartbeat_interval_sec 对齐的固定兜底
}

func secondsToDuration(s float64) time.Duration {
	return time.Duration(s * float64(time.Second))
}
