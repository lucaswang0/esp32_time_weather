"""路径解析：打包后与开发时都以 exe/项目根目录作为配置与日志的存放位置。"""
import sys
from pathlib import Path


def get_app_dir() -> Path:
    """返回应用目录：PyInstaller 打包后为 exe 所在目录，开发时为项目根目录。"""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[1]


APP_DIR = get_app_dir()
CONFIG_PATH = APP_DIR / "config.yaml"
LOG_DIR = APP_DIR / "logs"
LOG_PATH = LOG_DIR / "app.log"
