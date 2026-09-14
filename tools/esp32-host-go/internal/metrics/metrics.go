// Package metrics 提供进程内的运行时指标（FPS、队列长度、当前阈值等）。
//
// M1 仅做基础结构定义；M2 接入 pipeline 真实采样，
// M5 接入 Fyne 状态栏/日志面板展示。
package metrics

import (
	"math"
	"sync"
	"sync/atomic"
)

// PipelineMetrics 单个 pipeline 的运行时指标。
// 所有读方法线程安全；写方法应在 pipeline 的对应 goroutine 中调用。
type PipelineMetrics struct {
	// 计数器（atomic int64）
	FramesGenerated  atomic.Int64
	FramesProcessed  atomic.Int64
	ChunksSent       atomic.Int64
	ConnectionErrors atomic.Int64
	Reconnections    atomic.Int64
	SourceErrors     atomic.Int64 // source.Next 返回错误（含超时）
	SendErrors       atomic.Int64 // 协议包发送失败（连接层）
	PanicRecovered   atomic.Int64 // loop panic 恢复次数

	// Gauge（atomic）
	CurrentFPS        atomic.Uint64 // float64 bits
	CurrentThreshold  atomic.Int64
	FramesQueueSize    atomic.Int64
	LastFrameBytesSent atomic.Int64

	// 简单历史
	mu        sync.Mutex
	fpsWindow []float64
}

// New 构造一个零值 PipelineMetrics。
func New() *PipelineMetrics {
	return &PipelineMetrics{
		fpsWindow: make([]float64, 0, 32),
	}
}

// SetFPS 写入当前 FPS（线程安全）。
func (m *PipelineMetrics) SetFPS(fps float64) {
	m.CurrentFPS.Store(math.Float64bits(fps))
}

// FPS 读取最近一次写入的 FPS。
func (m *PipelineMetrics) FPS() float64 {
	return math.Float64frombits(m.CurrentFPS.Load())
}

// RecordSample 记录一次帧时间（秒），用于滑动平均。
func (m *PipelineMetrics) RecordSample(secs float64) {
	if secs <= 0 {
		return
	}
	m.mu.Lock()
	m.fpsWindow = append(m.fpsWindow, 1.0/secs)
	if len(m.fpsWindow) > 32 {
		m.fpsWindow = m.fpsWindow[len(m.fpsWindow)-32:]
	}
	m.mu.Unlock()
}

// AvgFPS 返回滑动窗口内的平均 FPS。
func (m *PipelineMetrics) AvgFPS() float64 {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.fpsWindow) == 0 {
		return 0
	}
	var sum float64
	for _, v := range m.fpsWindow {
		sum += v
	}
	return sum / float64(len(m.fpsWindow))
}
