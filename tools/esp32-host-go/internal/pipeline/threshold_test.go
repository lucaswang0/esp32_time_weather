package pipeline

import (
	"testing"
	"time"
)

// TestThresholdController_Record_BelowTarget 低于 target FPS 时阈值应上升。
func TestThresholdController_Record_BelowTarget(t *testing.T) {
	tc := NewThresholdController(1, 200, 5, 24.0, 0.1, 10, 5, 3)
	// 模拟 20 FPS（低于 24 - 2.4 = 21.6）：100ms 处理一次
	for i := 0; i < 5; i++ {
		tc.Record(100 * time.Millisecond)
	}
	got := tc.Current()
	if got <= 1 {
		t.Fatalf("expected threshold to grow, got %d", got)
	}
}

// TestThresholdController_Record_AboveTarget 高于 target FPS 时阈值应下降。
func TestThresholdController_Record_AboveTarget(t *testing.T) {
	tc := NewThresholdController(1, 200, 5, 24.0, 0.1, 10, 5, 3)
	// 起始值是 min=1，先手动设个较高的初始值
	tc.value = 50
	// 模拟 30 FPS（高于 24 + 2.4 = 26.4）：33ms 处理一次
	for i := 0; i < 5; i++ {
		tc.Record(33 * time.Millisecond)
	}
	if tc.Current() >= 50 {
		t.Fatalf("expected threshold to drop from 50, got %d", tc.Current())
	}
}

// TestThresholdController_Record_WithinHysteresis 接近 target 时不动。
func TestThresholdController_Record_WithinHysteresis(t *testing.T) {
	tc := NewThresholdController(1, 200, 5, 24.0, 0.1, 10, 5, 3)
	tc.value = 50
	// 模拟 24 FPS（恰好等于 target，不超过 ±hysteresis）
	for i := 0; i < 5; i++ {
		tc.Record(41667 * time.Microsecond) // ~1/24s
	}
	if tc.Current() != 50 {
		t.Fatalf("expected threshold unchanged at target, got %d", tc.Current())
	}
}

// TestThresholdController_Clamps 边界 clamp。
func TestThresholdController_Clamps(t *testing.T) {
	// min=10, max=20, step_up=50 → 单步就会触发到 max
	tc := NewThresholdController(10, 20, 50, 24.0, 0.1, 50, 5, 3)
	for i := 0; i < 5; i++ {
		tc.Record(200 * time.Millisecond) // 远低于 target
	}
	if tc.Current() != 20 {
		t.Fatalf("expected clamp at max=20, got %d", tc.Current())
	}
}
