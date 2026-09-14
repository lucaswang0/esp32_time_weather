# ESP32Stream 一键打包脚本（Windows / PowerShell）
# 用法（如遇执行策略拦截）：
#   powershell -ExecutionPolicy Bypass -File build\build.ps1
$ErrorActionPreference = "Stop"

# 切到项目根目录（本脚本在 build/ 下）
Set-Location (Join-Path $PSScriptRoot "..")

Write-Host "[1/3] 检查 PyInstaller..." -ForegroundColor Cyan
python -m PyInstaller --version
if ($LASTEXITCODE -ne 0) {
    python -m pip install pyinstaller
}

Write-Host "[2/3] 清理旧产物..." -ForegroundColor Cyan
if (Test-Path "build\pyi") { Remove-Item -Recurse -Force "build\pyi" }
if (Test-Path "dist\ESP32Stream.exe") { Remove-Item -Force "dist\ESP32Stream.exe" }

Write-Host "[3/3] 开始打包（onefile + windowed）..." -ForegroundColor Cyan
python -m PyInstaller --noconfirm --clean build\esp32_stream.spec
if ($LASTEXITCODE -ne 0) { throw "打包失败" }

Write-Host ""
Write-Host "完成！产物: $((Resolve-Path 'dist\ESP32Stream.exe').Path)" -ForegroundColor Green
Write-Host "分发时请把 config.yaml 与 ESP32Stream.exe 放同一目录（首次运行会自动生成）。" -ForegroundColor Yellow
