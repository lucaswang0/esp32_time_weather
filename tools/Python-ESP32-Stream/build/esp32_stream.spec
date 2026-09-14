# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller 规格：单文件 onefile + 无控制台 windowed。

构建：在项目根目录执行
    python -m PyInstaller --noconfirm --clean build/esp32_stream.spec
产物：dist/ESP32Stream.exe（与外部 config.yaml 放同一目录运行）
"""
import os
from PyInstaller.utils.hooks import collect_data_files, collect_submodules

# customtkinter 的主题 json/字体资源必须打入
datas = collect_data_files("customtkinter")

# LibreHardwareMonitor runtime DLL（pythonnet 加载）
_LHM_RUNTIME = r"C:\Users\user\AppData\Local\ESP32C3WirelessDisplay\sensor\LibreHardwareMonitor-0.9.6\runtime"
if os.path.isdir(_LHM_RUNTIME):
    for f in os.listdir(_LHM_RUNTIME):
        fp = os.path.join(_LHM_RUNTIME, f)
        if os.path.isfile(fp) and f.lower().endswith((".dll", ".exe.config", ".json")):
            datas.append((fp, "lhm_runtime"))

# 动态导入/后端模块需要显式声明
hiddenimports = [
    "mss.windows",          # mss 的 Windows 后端
    "pystray._win32",       # pystray 的 Win32 托盘后端
    "PIL._tkinter_finder",  # PIL ImageTk 支持（CTkImage 依赖）
    "clr",                  # pythonnet
]
hiddenimports += collect_submodules("clr_loader")

# 明确排除的大件/无用模块，缩小体积
excludes = [
    "numba", "llvmlite", "matplotlib", "scipy", "pandas", "pytest",
    "PyQt5", "PyQt6", "PySide6", "IPython", "jupyter", "notebook",
]

a = Analysis(
    ["..\\app\\main.py"],
    pathex=[".."],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=excludes,
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="ESP32Stream",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,          # 无控制台黑窗（纯 GUI + 托盘）
    disable_windowed_traceback=False,
    icon=None,
)
