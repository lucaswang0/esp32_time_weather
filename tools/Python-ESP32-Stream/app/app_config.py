"""应用配置：默认值、YAML 加载/保存、深合并、校验与 pipeline 扁平化。"""
from __future__ import annotations

from copy import deepcopy
from typing import Any

import yaml

from .paths import CONFIG_PATH

VALID_SOURCES = ("desktop", "window", "dashboard")
VALID_ALIGN = ("left", "center", "right")


def _default_widgets() -> list[dict[str, Any]]:
    """仪表盘组件默认定义（顺序即默认显示顺序）。

    x/y 为 null 表示参与自动纵向排列；填整数则为画布绝对坐标（像素）。
    w 为 null 表示按内容自适应宽度；填整数为固定像素宽（不再占满整行）。
    scale 为组件缩放倍率（0.8–2.0）。
    """
    return [
        {"type": "clock", "enabled": True, "color": "#FFD700",
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "cpu", "enabled": True, "color": "#00E676", "sparkline": True,
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "memory", "enabled": True, "color": "#40C4FF",
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "disk", "enabled": True, "color": "#FFAB40", "path": "C:\\",
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "net", "enabled": True, "color": "#E040FB", "adapter": None,
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "temp", "enabled": True, "color": "#FF7043",
         "temp_enabled": {},
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "uptime", "enabled": False, "color": "#FFFFFF",
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "ip", "enabled": False, "color": "#AAAAAA",
         "x": None, "y": None, "w": None, "scale": 1.0},
        {"type": "custom_text", "enabled": False, "color": "#FFFFFF", "text": "",
         "x": None, "y": None, "w": None, "scale": 1.0},
    ]


def default_config() -> dict[str, Any]:
    """返回一份全新的默认配置（深拷贝，避免共享可变对象）。"""
    return {
        "connection": {
            "esp32_host": "10.45.1.9",
            "esp32_port": 8888,
            "use_broadcast": True,
            "broadcast_port": 8889,
            "broadcast_hold_time": 30,
            "reconnect_interval_sec": 3.0,
            "socket_timeout": 1.0,
        },
        "video": {
            "target_width": 320,
            "target_height": 170,
            "target_fps": 24.0,
            "generator_target_interval_sec": 0.03,
            "max_chunk_data_size": 8192,
            "gamma": 1.2,
            "wb_scale": [1.1, 1.05, 0.95],
            "min_dirty_rect_threshold": 1,
            "max_dirty_rect_threshold": 180,
            "threshold_adjustment_step_up": 15,
            "threshold_adjustment_step_down": 5,
            "fps_history_size": 3,
            "fps_hysteresis_factor": 0.03,
            "frames_queue_max_size": 8,
            "generator_low_water_mark": 3,
            "heartbeat_interval_sec": 2.0,
        },
        "active_source": "window",
        "ui": {
            "close_to_tray": True,
            "start_minimized": False,
            "tray_switch_source": True,
        },
        "sources": {
            "desktop": {"monitor": 0, "region": None, "crop_alignment": "center"},
            "window": {"window_title": "任务管理器", "crop_alignment": "left"},
            "dashboard": {
                "background": "#0B0F14",
                "bg_image": None,
                "refresh_interval_sec": 1.0,
                "gap": 2,
                "widgets": _default_widgets(),
            },
        },
    }


def deep_merge(base: dict, override: dict) -> dict:
    """以 base 为底，用 override 递归覆盖；返回新 dict，不修改入参。"""
    result = deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def _normalize_widgets(cfg: dict[str, Any]) -> None:
    """按默认组件表补齐用户配置中缺失的组件，并过滤未知类型。"""
    dash = cfg["sources"]["dashboard"]
    defaults_list = _default_widgets()
    defaults_order = {w["type"]: i for i, w in enumerate(defaults_list)}
    defaults = {w["type"]: w for w in defaults_list}
    user_widgets = dash.get("widgets") or []
    by_type: dict[str, dict] = {}
    for w in user_widgets:
        if isinstance(w, dict) and w.get("type") in defaults:
            merged = deep_merge(defaults[w["type"]], w)
            by_type[w["type"]] = merged
    # 用户已有组件按其出现顺序（去重）
    ordered: list[dict] = []
    seen: set[str] = set()
    for w in user_widgets:
        t = w.get("type") if isinstance(w, dict) else None
        if t in by_type and t not in seen:
            seen.add(t)
            ordered.append(by_type[t])
    # 缺失类型插入到默认顺序中的相对位置（排在最后一个已存在的前序组件之后）
    for i, default_w in enumerate(_default_widgets()):
        t = default_w["type"]
        if t in seen:
            continue
        insert_at = len(ordered)
        for j, existing in enumerate(ordered):
            if defaults_order[existing["type"]] > i:
                insert_at = j
                break
        ordered.insert(insert_at, deepcopy(default_w))
        seen.add(t)
    dash["widgets"] = ordered


def validate(cfg: dict[str, Any]) -> list[str]:
    """校验并就地修正配置，返回警告信息列表。"""
    warnings: list[str] = []
    if cfg["active_source"] not in VALID_SOURCES:
        warnings.append(f"未知 active_source={cfg['active_source']}，回退 window")
        cfg["active_source"] = "window"
    w, h = cfg["video"]["target_width"], cfg["video"]["target_height"]
    if not (0 < w <= 1024 and 0 < h <= 1024):
        warnings.append(f"非法分辨率 {w}x{h}，回退 320x170")
        cfg["video"]["target_width"], cfg["video"]["target_height"] = 320, 170
    for sec in ("desktop", "window"):
        align = cfg["sources"][sec].get("crop_alignment")
        if align not in VALID_ALIGN:
            cfg["sources"][sec]["crop_alignment"] = "center"
    wb = cfg["video"].get("wb_scale")
    if not (isinstance(wb, list) and len(wb) == 3):
        warnings.append("wb_scale 非法，回退 [1,1,1]")
        cfg["video"]["wb_scale"] = [1.0, 1.0, 1.0]
    _normalize_widgets(cfg)
    _sanitize_widget_layout(cfg, warnings)
    return warnings


def _sanitize_widget_layout(cfg: dict, warnings: list[str]) -> None:
    """修正每个组件的 x/y/scale 非法值。"""
    for w in cfg["sources"]["dashboard"]["widgets"]:
        try:
            scale = float(w.get("scale", 1.0))
            w["scale"] = min(2.0, max(0.8, scale))
        except (TypeError, ValueError):
            warnings.append(f"组件 {w.get('type')} scale 非法，回退 1.0")
            w["scale"] = 1.0
        for axis in ("x", "y", "w"):
            val = w.get(axis)
            if val is None:
                continue
            try:
                w[axis] = max(0, int(val))
            except (TypeError, ValueError):
                w[axis] = None
    img = cfg["sources"]["dashboard"].get("bg_image")
    if img is not None and not isinstance(img, str):
        cfg["sources"]["dashboard"]["bg_image"] = None
    try:
        gap = int(cfg["sources"]["dashboard"].get("gap", 2))
        cfg["sources"]["dashboard"]["gap"] = min(50, max(0, gap))
    except (TypeError, ValueError):
        warnings.append("dashboard.gap 非法，回退 2")
        cfg["sources"]["dashboard"]["gap"] = 2


def load_config(path=CONFIG_PATH) -> dict[str, Any]:
    """加载 yaml 配置；文件不存在时写入并返回默认配置。"""
    if not path.exists():
        cfg = default_config()
        save_config(cfg, path)
        return cfg
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f) or {}
    cfg = deep_merge(default_config(), raw)
    # 丢弃旧版/未知顶层键，保证写回的配置结构干净
    cfg = {k: v for k, v in cfg.items() if k in default_config()}
    return cfg


def save_config(cfg: dict[str, Any], path=CONFIG_PATH) -> None:
    """保存配置到 yaml（utf-8、保留中文、块式列表）。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, allow_unicode=True, sort_keys=False,
                       default_flow_style=False)


def build_pipeline_settings(cfg: dict[str, Any]) -> dict[str, Any]:
    """把 connection+video 扁平化成 pipeline 使用的设置快照。"""
    settings: dict[str, Any] = {}
    settings.update(cfg["connection"])
    settings.update(cfg["video"])
    settings["wb_scale"] = tuple(cfg["video"]["wb_scale"])
    return settings
