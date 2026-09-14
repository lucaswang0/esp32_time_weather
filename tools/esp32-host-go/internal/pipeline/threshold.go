package pipeline

import (
	"math"
	"sync"
	"time"
)

// ThresholdController 自适应阈值控制器。
//
// 与 Python pipeline.py _consumer_loop 末尾逻辑等价：
//
//	current_fps = 1.0 / avg_processing_time
//	if current_fps < target - hysteresis: threshold += step_up (cap at max)
//	elif current_fps > target + hysteresis: threshold -= step_down (floor at min)
//
// hysteresis 默认为 target * 0.1，与 Python 版 fps_hysteresis_factor=0.1 对齐。
type ThresholdController struct {
	mu sync.Mutex

	min   int
	max   int
	step  int
	value int

	targetFPS    float64
	hysteresis   float64
	stepUp       int
	stepDown     int

	history []time.Duration
	histSize int

	lastFPS float64
}

// NewThresholdController 构造控制器。
func NewThresholdController(min, max, step int, targetFPS, hysteresis float64, stepUp, stepDown, historySize int) *ThresholdController {
	if min < 0 {
		min = 0
	}
	if max < min {
		max = min
	}
	if stepUp <= 0 {
		stepUp = 10
	}
	if stepDown <= 0 {
		stepDown = 5
	}
	if historySize <= 0 {
		historySize = 3
	}
	if hysteresis <= 0 {
		hysteresis = targetFPS * 0.1
	}
	return &ThresholdController{
		min:       min,
		max:       max,
		step:      step,
		value:     min,
		targetFPS: targetFPS,
		hysteresis: hysteresis,
		stepUp:    stepUp,
		stepDown:  stepDown,
		history:   make([]time.Duration, 0, historySize),
		histSize:  historySize,
	}
}

// Current 返回当前阈值（线程安全）。
func (t *ThresholdController) Current() int {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.value
}

// Record 记录一次帧处理时长，按滑动窗口更新阈值。
// 至少 historySize 次采样后才会调整阈值（与 Python 行为一致）。
func (t *ThresholdController) Record(d time.Duration) {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.history = append(t.history, d)
	if len(t.history) > t.histSize {
		t.history = t.history[len(t.history)-t.histSize:]
	}
	if len(t.history) < t.histSize {
		return
	}

	var sum time.Duration
	for _, v := range t.history {
		sum += v
	}
	avg := sum / time.Duration(len(t.history))
	if avg <= 0 {
		return
	}
	fps := 1.0 / avg.Seconds()
	t.lastFPS = fps

	old := t.value
	if fps < t.targetFPS-t.hysteresis {
		t.value += t.stepUp
		if t.value > t.max {
			t.value = t.max
		}
	} else if fps > t.targetFPS+t.hysteresis {
		t.value -= t.stepDown
		if t.value < t.min {
			t.value = t.min
		}
	}
	_ = old
	_ = math.Abs // 保留 math 包引用以便未来扩展
}

// LastFPS 返回最近一次计算得到的 FPS。
func (t *ThresholdController) LastFPS() float64 {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.lastFPS
}
