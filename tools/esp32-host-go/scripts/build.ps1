# esp32-host-go Windows 构建脚本
#
# 用法（在 PowerShell 中执行）：
#   .\scripts\build.ps1
#
# 产物：dist\esp32-host-go.exe

$ErrorActionPreference = "Stop"
Set-Location -Path (Join-Path $PSScriptRoot "..")

$go = (Get-Command go -ErrorAction SilentlyContinue)
if (-not $go) {
    Write-Error "go toolchain not found in PATH. Install Go 1.22+ from https://go.dev/dl/"
    exit 1
}

Write-Host "==> go version"
& go version

Write-Host "==> go mod tidy"
& go mod tidy

Write-Host "==> go test ./internal/..."
& go test ./internal/...

Write-Host "==> go build"
New-Item -ItemType Directory -Path "dist" -Force | Out-Null
& go build -trimpath -ldflags "-s -w" -o "dist\esp32-host-go.exe" .

Write-Host "==> done: dist\esp32-host-go.exe"
Get-Item "dist\esp32-host-go.exe" | Select-Object Name, Length
