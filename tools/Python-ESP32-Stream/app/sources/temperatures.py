"""Windows 传感器采集：多后端自动探测，统一输出 {key,kind,sensor_type,label,value,unit}。

后端（按优先级）：
1. LibreHardwareMonitorLib.dll（通过 pythonnet/clr 直接加载，无需运行 LHM GUI）
   - 可提供 CPU/主板/显卡/硬盘的 温度/电压/风扇/功耗/频率/负载 等全部传感器
   - 需要管理员权限才能读取全部传感器（CPU 温度、主板电压等）
2. LibreHardwareMonitor/OpenHardwareMonitor WMI（回退，需运行 LHM GUI）
3. nvidia-smi（NVIDIA 显卡温度，无需管理员）
4. MSAcpi_ThermalZoneTemperature（ACPI 热区）
5. Get-StorageReliabilityCounter（硬盘温度）

子进程调用较重，结果按 ttl 秒缓存。
"""
from __future__ import annotations

import logging
import os
import shutil
import subprocess
import sys
import threading
import time

log = logging.getLogger(__name__)

KIND_LABEL = {"cpu": "CPU", "mb": "主板", "gpu": "显卡",
              "disk": "硬盘", "mem": "内存", "zone": "热区"}

# LHM SensorType -> 显示单位
_SENSOR_UNITS = {
    "Temperature": "°C",
    "Voltage": "V",
    "Fan": "RPM",
    "Power": "W",
    "Clock": "MHz",
    "Load": "%",
    "Flow": "L/h",
    "Control": "%",
    "Level": "%",
    "Data": "GB",
    "SmallData": "MB",
    "Throughput": "MB/s",
    "Frequency": "Hz",
}

# HardwareType -> kind
_HW_TYPE_KIND = {
    "Cpu": "cpu",
    "GpuNvidia": "gpu",
    "GpuAmd": "gpu",
    "GpuIntel": "gpu",
    "Memory": "mem",
    "Storage": "disk",
    "Motherboard": "mb",
    "SuperIO": "mb",
    "EmbeddedController": "mb",
    "Cooler": "mb",
    "Psu": "mb",
    "Battery": "mb",
    "Network": "mb",
}

# ---------- LHM DLL 直接读取（首选） ----------

def _lhm_dll_candidates() -> list[str]:
    """返回 LHM DLL 候选路径列表（按优先级）。"""
    cands = []
    # 1. 用户配置路径（由 set_dll_path 注入）
    if _dll_path:
        cands.append(_dll_path)
    # 2. PyInstaller 打包后的临时目录
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        cands.append(os.path.join(meipass, "lhm_runtime",
                                  "LibreHardwareMonitorLib.dll"))
    # 3. 应用同级 runtime 目录
    cands.append(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "..", "runtime",
                              "LibreHardwareMonitorLib.dll"))
    # 4. 常见 LHM 安装路径
    cands.append(r"C:\Program Files\LibreHardwareMonitor\LibreHardwareMonitorLib.dll")
    cands.append(r"C:\Program Files (x86)\LibreHardwareMonitor\LibreHardwareMonitorLib.dll")
    # 5. ESP32C3WirelessDisplay 自带的 LHM
    cands.append(r"C:\Users\user\AppData\Local\ESP32C3WirelessDisplay\sensor\LibreHardwareMonitor-0.9.6\runtime\LibreHardwareMonitorLib.dll")
    return cands


_dll_path: str | None = None
_clr_loaded = False
_lhm_computer = None
_lhm_available = False
_lhm_init_error: str | None = None


def set_dll_path(path: str | None) -> None:
    """设置 LHM DLL 路径（在首次采集前调用）。"""
    global _dll_path
    _dll_path = path


def _find_dll() -> str | None:
    """按候选列表查找 LibreHardwareMonitorLib.dll。"""
    for cand in _lhm_dll_candidates():
        if cand and os.path.isfile(cand):
            return cand
    return None


def _init_lhm() -> bool:
    """加载 LHM DLL 并初始化 Computer 实例。成功返回 True。"""
    global _clr_loaded, _lhm_computer, _lhm_available, _lhm_init_error
    if _lhm_available:
        return True
    if _lhm_init_error:
        return False
    dll = _find_dll()
    if not dll:
        _lhm_init_error = "LibreHardwareMonitorLib.dll 未找到"
        log.warning("LHM DLL 不可用: %s", _lhm_init_error)
        return False
    try:
        import clr
        import sys
        dll_dir = os.path.dirname(dll)
        if dll_dir not in sys.path:
            sys.path.insert(0, dll_dir)
        clr.AddReference("LibreHardwareMonitorLib")
        _clr_loaded = True
        from LibreHardwareMonitor import Hardware
        computer = Hardware.Computer()
        computer.IsCpuEnabled = True
        computer.IsGpuEnabled = True
        computer.IsMemoryEnabled = True
        computer.IsMotherboardEnabled = True
        computer.IsStorageEnabled = True
        computer.IsNetworkEnabled = False
        computer.Open()
        _lhm_computer = computer
        _lhm_available = True
        log.info("LHM DLL 初始化成功: %s", dll)
        return True
    except Exception as e:
        _lhm_init_error = str(e)
        log.warning("LHM DLL 初始化失败: %s", e)
        return False


def _walk_lhm(hw, out: list[dict]) -> None:
    """递归遍历硬件及其子硬件，收集所有传感器。"""
    try:
        hw.Update()
    except Exception:
        pass
    hw_type = str(hw.HardwareType)
    kind = _HW_TYPE_KIND.get(hw_type, "mb")
    hw_name = str(hw.Name)
    kind_label = KIND_LABEL.get(kind, "传感器")
    for sensor in hw.Sensors:
        try:
            stype = str(sensor.SensorType)
            val = sensor.Value
            # System.Nullable<double> -> Python float or None
            if val is None:
                continue
            raw = float(val)
            value = _format_value(raw, stype)
            name = str(sensor.Name)
            identifier = str(sensor.Identifier)
            out.append({
                "key": f"lhm:{identifier}",
                "kind": kind,
                "sensor_type": stype,
                "label": f"{kind_label} {name}",
                "value": value,
                "unit": _SENSOR_UNITS.get(stype, ""),
            })
        except Exception:
            continue
    for sub in hw.SubHardware:
        _walk_lhm(sub, out)


def _collect_lhm_dll() -> list[dict]:
    """通过 LHM DLL 采集全部传感器。失败返回空列表。"""
    if not _init_lhm():
        return []
    sensors: list[dict] = []
    try:
        for hw in _lhm_computer.Hardware:
            _walk_lhm(hw, sensors)
    except Exception as e:
        log.warning("LHM DLL 采集异常: %s", e)
    return sensors


# ---------- WMI 回退 ----------

_PS_SCRIPT = r"""
$ErrorActionPreference='SilentlyContinue'
$ci = [System.Globalization.CultureInfo]::InvariantCulture
Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName Sensor |
  ForEach-Object { 'LHM|' + $_.Parent + '|' + $_.Name + '|' + $_.SensorType + '|' + $_.Value.ToString($ci) }
Get-CimInstance -Namespace root/OpenHardwareMonitor -ClassName Sensor |
  ForEach-Object { 'OHM|' + $_.Parent + '|' + $_.Name + '|' + $_.SensorType + '|' + $_.Value.ToString($ci) }
Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature |
  ForEach-Object { 'TZ|' + $_.InstanceName + '|' + $_.CurrentTemperature }
Get-PhysicalDisk | ForEach-Object {
  $r = $_ | Get-StorageReliabilityCounter
  'DISK|' + $_.DeviceId + '|' + $_.FriendlyName + '|' + $r.Temperature
}
""".strip()


def _run_ps() -> str:
    """执行 PowerShell 查询并返回 stdout；失败返回空串。"""
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-Command", _PS_SCRIPT],
            capture_output=True, text=True, timeout=15,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return out.stdout or ""
    except (OSError, subprocess.SubprocessError) as e:
        log.warning("传感器 WMI 查询失败: %s", e)
        return ""


def _run_nvidia_smi() -> list[str]:
    """nvidia-smi 查询每块 N 卡温度，返回 'GPU|index|name|temp' 行列表。"""
    exe = shutil.which("nvidia-smi")
    if not exe:
        return []
    try:
        out = subprocess.run(
            [exe, "--query-gpu=index,name,temperature.gpu",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=6,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except (OSError, subprocess.SubprocessError):
        return []
    rows = []
    for line in (out.stdout or "").splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) == 3 and parts[2].lstrip("-").isdigit():
            rows.append("GPU|" + parts[0] + "|" + parts[1] + "|" + parts[2])
    return rows


def _lhm_kind(parent: str, name: str) -> str:
    """按 LHM 父节点路径判断传感器所属硬件类别（WMI 回退用）。"""
    text = (parent + " " + name).lower()
    if "gpu" in text or "nvidia" in text or ("amd" in text and "gpu" in text):
        return "gpu"
    if "cpu" in text:
        return "cpu"
    if any(k in text for k in ("hdd", "ssd", "nvme", "storage", "/storage")):
        return "disk"
    if "ram" in text or "memory" in text or "/ram" in text:
        return "mem"
    if any(k in text for k in ("motherboard", "mainboard", "lpc", "superio",
                                "acpi", "embeddedcontroller")):
        return "mb"
    return "mb"


def _parse_float(value: str):
    """容错浮点数解析：标准浮点失败时尝试逗号小数点等区域格式。"""
    try:
        return float(value)
    except (ValueError, TypeError):
        try:
            return float(str(value).replace(",", "."))
        except (ValueError, TypeError):
            return None


def _format_value(value: float, sensor_type: str) -> float:
    """按传感器类型对数值做合理取整（含单位换算）。"""
    if sensor_type == "Temperature":
        return round(value)
    if sensor_type == "Voltage":
        return round(value, 3)
    if sensor_type in ("Fan", "Clock", "Load", "Flow", "Control", "Level"):
        return round(value)
    if sensor_type == "Power":
        return round(value, 1)
    if sensor_type == "Throughput":
        # LHM 返回字节/秒，换算为 MB/s
        return round(value / (1024 * 1024), 1)
    if sensor_type in ("Data", "SmallData"):
        return round(value, 2)
    return round(value, 2)


class TemperatureMonitor:
    """传感器采集器：后台 daemon 线程周期扫描，scan() 立即返回缓存（不阻塞）。

    虽然类名保留为 TemperatureMonitor（向后兼容），但实际采集 LHM
    暴露的全部传感器类型：温度/电压/风扇/功耗/频率/负载等。
    """

    def __init__(self, ttl: float = 3.0):
        self._ttl = ttl
        self._lock = threading.Lock()
        self._sensors: list[dict] = []
        self._last_ts = 0.0
        self._scanning = False
        self._refs = 0
        self._stop = threading.Event()
        self._kick = False
        self._thread: threading.Thread | None = None
        self._logged_raw = False

    def start(self) -> None:
        """引用计数 +1，首个使用者启动后台扫描线程（线程常驻）。"""
        with self._lock:
            first = self._thread is None
            self._refs += 1
        if first:
            self._stop.clear()
            self._thread = threading.Thread(
                target=self._loop, daemon=True, name="SensorScan")
            self._thread.start()

    def stop(self) -> None:
        """引用计数 -1；归零后线程转为空闲（不退出进程，daemon 随主程序结束）。"""
        with self._lock:
            self._refs = max(0, self._refs - 1)

    def scan_once_async(self) -> None:
        """请求一次尽快扫描（下一轮循环立即执行），供 GUI 刷新按钮。"""
        with self._lock:
            self._kick = True

    def scan(self, force: bool = False) -> list[dict]:
        """立即返回当前缓存（可能为空，首个扫描周期后填充）。"""
        with self._lock:
            return list(self._sensors)

    @property
    def has_scanned(self) -> bool:
        """是否已完成至少一轮扫描。"""
        with self._lock:
            return self._last_ts > 0

    def _loop(self) -> None:
        while not self._stop.is_set():
            active = self._refs > 0
            kick = self._kick
            if active or kick:
                with self._lock:
                    self._kick = False
                if not self._scanning:
                    self._scanning = True
                    try:
                        sensors = self._collect()
                        with self._lock:
                            self._sensors = sensors
                            self._last_ts = time.monotonic()
                    except Exception:
                        log.exception("传感器扫描异常")
                    finally:
                        self._scanning = False
            self._stop.wait(self._ttl)

    def _collect(self) -> list[dict]:
        """跑一次全部后端。LHM DLL 优先，失败回退 WMI。"""
        # 1. LHM DLL 直接读取
        sensors = _collect_lhm_dll()
        if sensors:
            if not self._logged_raw:
                self._logged_raw = True
                log.info("LHM DLL 采集到 %d 个传感器", len(sensors))
            return sensors

        # 2. WMI 回退
        lines = _run_ps().splitlines()
        if not self._logged_raw:
            self._logged_raw = True
            log.info("LHM/OHM WMI 原始输出前 20 行:\n  %s",
                     "\n  ".join(lines[:20]))
        wmi_sensors: list[dict] = []
        has_lhm = False
        for line in lines:
            parts = line.strip().split("|")
            tag = parts[0]
            if tag == "LHM" and len(parts) >= 5:
                has_lhm = True
                wmi_sensors.append(self._mk_lhm(parts[1], parts[2], parts[3],
                                                parts[4], "lhm"))
            elif tag == "OHM" and len(parts) >= 5:
                has_lhm = True
                wmi_sensors.append(self._mk_lhm(parts[1], parts[2], parts[3],
                                                parts[4], "ohm"))
        if has_lhm:
            return wmi_sensors
        for line in lines:
            parts = line.strip().split("|")
            if parts[0] == "TZ" and len(parts) >= 3:
                self._parse_tz(parts, wmi_sensors)
            elif parts[0] == "DISK" and len(parts) >= 4:
                self._parse_disk(parts, wmi_sensors)
        wmi_sensors.extend(self._nvidia_gpus())
        return wmi_sensors

    @staticmethod
    def _mk_lhm(parent: str, name: str, sensor_type: str, value: str,
                src: str) -> dict:
        raw = _parse_float(value)
        val = _format_value(raw, sensor_type) if raw is not None else None
        kind = _lhm_kind(parent, name)
        unit = _SENSOR_UNITS.get(sensor_type, "")
        return {
            "key": f"{src}:{parent}/{name}",
            "kind": kind,
            "sensor_type": sensor_type,
            "label": f"{KIND_LABEL.get(kind, '传感器')} {name}",
            "value": val,
            "unit": unit,
        }

    @staticmethod
    def _parse_tz(parts: list[str], out: list[dict]) -> None:
        try:
            temp = round(int(parts[2]) / 10.0 - 273.15)
        except ValueError:
            return
        short = parts[1].replace("\\", "/").split("/")[-1]
        out.append({"key": f"zone:{short}", "kind": "zone",
                    "sensor_type": "Temperature",
                    "label": f"热区 {short}", "value": temp, "unit": "°C"})

    @staticmethod
    def _parse_disk(parts: list[str], out: list[dict]) -> None:
        raw = parts[3].strip()
        if not raw or not raw.replace(".", "").lstrip("-").isdigit():
            return
        dev = parts[1].strip()
        name = parts[2].strip()
        out.append({"key": f"disk:{dev}", "kind": "disk",
                    "sensor_type": "Temperature",
                    "label": f"硬盘 {name[:14]}", "value": round(float(raw)),
                    "unit": "°C"})

    @staticmethod
    def _nvidia_gpus() -> list[dict]:
        rows = []
        for line in _run_nvidia_smi():
            parts = line.split("|")
            if len(parts) != 4:
                continue
            _, idx, name, temp = parts
            short = name.strip()
            for prefix in ("NVIDIA GeForce ", "GeForce ", "NVIDIA ", "AMD "):
                if short.startswith(prefix):
                    short = short[len(prefix):]
                    break
            rows.append({"key": f"gpu-nvidia:{idx}", "kind": "gpu",
                         "sensor_type": "Temperature",
                         "label": f"显卡 {short[:16]}",
                         "value": int(temp), "unit": "°C"})
        return rows
