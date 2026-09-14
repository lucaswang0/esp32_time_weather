//go:build !windows

package source

import (
	"esp32_host/internal/config"
)

// NewSysInfoSource 非 Windows 平台暂未实现；targetW/H 仅为与 windows 版签名对齐。
func NewSysInfoSource(cfg config.SysInfoOpts, targetW, targetH int) Source {
	return &unsupportedSource{platform: "non-Windows"}
}

var _ config.SysInfoOpts // 防止 cfg 暂未使用时被 vet 报警
