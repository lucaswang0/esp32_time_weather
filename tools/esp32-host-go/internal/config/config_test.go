package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// TestSysInfoWidget_CoordsRoundTrip 验证 widget 坐标经 Save/Load 往返后保留。
func TestSysInfoWidget_CoordsRoundTrip(t *testing.T) {
	orig := &Config{
		Pipelines: []PipelineConf{
			{
				Name: "Dash",
				Target: Target{Width: 320, Height: 170},
				Source: Source{
					Type: "sysinfo",
					SysInfo: SysInfoOpts{
						RefreshMs: 1000,
						Widgets: []SysInfoWidget{
							{Type: "cpu", Name: "CPU", X: 2, Y: 3, W: 100, H: 40},
							{Type: "text", Text: "STATUS", X: 0, Y: 0, W: 320, H: 20},
							{Type: "mem"}, // 无坐标 → 栈式布局
						},
					},
				},
			},
		},
	}

	dir := t.TempDir()
	path := filepath.Join(dir, "config.yaml")
	if err := orig.Save(path); err != nil {
		t.Fatalf("save: %v", err)
	}

	loaded, err := Load(path)
	if err != nil {
		t.Fatalf("load: %v", err)
	}
	got := loaded.Pipelines[0].Source.SysInfo.Widgets
	want := orig.Pipelines[0].Source.SysInfo.Widgets
	if len(got) != len(want) {
		t.Fatalf("widget count = %d, want %d", len(got), len(want))
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("widget[%d] = %+v, want %+v", i, got[i], want[i])
		}
	}
}

// TestSysInfoWidget_YAML_OMitempty 验证零值坐标不写出多余字段。
func TestSysInfoWidget_YAML_OMitempty(t *testing.T) {
	cfg := &Config{
		Pipelines: []PipelineConf{
			{
				Name:   "Dash",
				Target: Target{Width: 170, Height: 320},
				Source: Source{
					Type:    "sysinfo",
					SysInfo: SysInfoOpts{Widgets: []SysInfoWidget{{Type: "cpu"}}},
				},
			},
		},
	}
	dir := t.TempDir()
	path := filepath.Join(dir, "config.yaml")
	if err := cfg.Save(path); err != nil {
		t.Fatalf("save: %v", err)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	for _, key := range []string{"x:", "y:", "w:", "h:"} {
		for _, line := range strings.Split(string(raw), "\n") {
			if strings.HasPrefix(strings.TrimSpace(line), key) {
				t.Errorf("unexpected zero-value key %q in yaml:\n%s", key, raw)
			}
		}
	}
}
