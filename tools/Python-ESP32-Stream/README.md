# ESP32 Stream — 屏幕推流上位机（GUI）

PC 端通过 Wi-Fi(TCP) 把画面实时推送到 ESP32 + TFT 屏。纯 GUI 应用（无控制台），
支持**桌面 / 指定窗口 / 仪表盘**三种画面源运行时热切换，可最小化到系统托盘，
打包为单个 `ESP32Stream.exe`，配置全部保存在 exe 同目录的 `config.yaml`。

## 功能

- **三种画面源（运行时一键切换，TCP 不断开，切换后首帧全量刷新）**
  - **桌面**：选择显示器或自定义物理像素区域，支持 left/center/right 裁剪对齐
  - **指定窗口**：GUI 内刷新/搜索窗口列表，选中即推，窗口丢失自动等待恢复
  - **仪表盘**：本地系统指标（psutil 渲染），可勾选/排序/改色/选参数的组件：
    时钟、CPU（含历史曲线图）、内存、磁盘分区、网卡上下行速率、
    **温度**（显卡/CPU/主板/硬盘，传感器逐个勾选）、运行时间、IP、自定义文本；
    每个组件支持 **0.8–2.0× 缩放**与**自由定位**（在 2× 预览上直接拖拽，或在卡片里填 x/y）；
    组件宽度按**内容自适应、不再占满整行**，可拖预览中选中框的右边缘或填"宽"像素固定；
    不需要定位的组件勾"自动"即可参与纵向排列，"自动排列全部"一键复位；
    自动排列的**组件间距**可在"间距"框设置（0–50 像素，默认 2，超画布高度会红字提示）；
    背景支持**纯色取色或本地图片**（等比裁剪填充画布，可一键清除回纯色）
- **连接**：配置 IP/端口直连 + UDP 广播自动发现 ESP32（广播 IP 有效期内覆盖配置 IP），
  断线自动重连；状态栏实时显示连接灯/FPS/帧计数
- **画质**：伽马、白平衡、目标 FPS、脏矩形自适应阈值、RGB565 分包均可在界面调整
- **系统托盘**：关闭窗口默认驻留托盘（推流不中断），托盘菜单可启停/切换画面源，
  图标颜色表示连接状态，"退出"才真正结束
- **协议**：与现有 ESP32 固件完全兼容（固件零改动，见文末协议说明）

## 目录结构

```
app/                  应用源码（Python 包）
  main.py             入口（PyInstaller 入口脚本，绝对导入）
  paths.py            exe 同目录路径解析（配置/日志外置）
  app_config.py       配置模型/默认值/YAML 读写/校验
  controller.py       GUI 门面：启停、热切换、整体重启
  pipeline.py         连接管理/重连/帧源热切换编排
  consumer.py         差分/自适应阈值/分包发送/心跳
  discovery.py        UDP 广播发现
  imaging.py          numpy 向量化：伽马/WB/脏矩形/RGB565/打包
  tray.py             pystray 系统托盘
  sources/            三种帧源 + 仪表盘组件
  gui/                CustomTkinter 界面（主窗口/三面板/状态栏/日志/预览）
build/esp32_stream.spec   PyInstaller 规格（onefile + windowed）
build/build.ps1           一键打包脚本
tools/fake_esp32_receiver.py  本地假 ESP32 接收端（开发/排障用，不打包）
config.yaml           外部配置（首次运行自动生成默认值）
```

## 开发模式运行

需要 Python 3.10+（在 3.13 上验证）。

```bash
pip install -r requirements.txt
python -m app.main
```

开发期可用假接收端验证协议：

```bash
python tools/fake_esp32_receiver.py 8899   # 先把 config.yaml 里 IP 改为 127.0.0.1、端口 8899
```

## 配置文件（config.yaml）

首次运行自动在 **exe/项目根目录**生成；GUI 中"应用/保存"会自动写回。

| 段 | 关键项 |
|---|---|
| `connection` | `esp32_host`、`esp32_port`、`use_broadcast`、`broadcast_port`、`broadcast_hold_time`、`reconnect_interval_sec`、`socket_timeout` |
| `video` | `target_width/height`（须与屏幕一致）、`target_fps`、`gamma`、`wb_scale[R,G,B]`、`max_chunk_data_size`（≤ ESP32 `MAX_CHUNK_SIZE`）、脏矩形阈值系列、心跳间隔 |
| `active_source` | `desktop` / `window` / `dashboard` |
| `ui` | `close_to_tray`（X 驻留托盘）、`start_minimized`、`tray_switch_source` |
| `sources.desktop` | `monitor`（0=所有显示器虚拟合集）、`region`（null 或 left/top/width/height 物理像素）、`crop_alignment` |
| `sources.window` | `window_title`（子串匹配）、`crop_alignment` |
| `sources.dashboard` | `background`（纯色）、`bg_image`（背景图片绝对路径，null=纯色）、`refresh_interval_sec`、`gap`（自动排列组件间距像素，默认 2）、`widgets[]` |
| `dashboard.widgets[]` | `type/enabled/color` 及各自参数；`x/y`（null=自动排列，整数=画布绝对像素定位）；`w`（null=按内容自适应宽度，整数=固定像素宽）；`scale`（0.8–2.0 缩放倍率） |

## 打包为 exe

```powershell
# 如 PowerShell 拦截脚本：
powershell -ExecutionPolicy Bypass -File build\build.ps1
```

产物 `dist/ESP32Stream.exe`（onefile + windowed，约 12MB）。
**分发时只需把 `ESP32Stream.exe` 拷贝给用户**：首次运行会在同目录自动生成
`config.yaml` 与 `logs/app.log`；也可以随 exe 附带一份预置好的 `config.yaml`。
onefile 首次启动需数秒解压，属正常现象；如被杀毒软件误报，可加白名单或改用 onedir。

## ESP32 端（协议说明，固件无需改动）

- ESP32 为 TCP server（默认 8888），PC 主动连接，设置 `TCP_NODELAY`
- 包格式：12 字节大端包头 `struct '!HHHH I'` = x, y, w, h, data_len，后接小端 RGB565
- 单包数据 ≤ `max_chunk_data_size`（默认 8192，须 ≤ 固件 `MAX_CHUNK_SIZE`）
- 心跳包：x=y=0xFFFF、w=h=0、data_len=0（画面静止时保活）
- 可选 UDP 广播：ESP32 周期发送文本 `ESP32:<ip>:<port>` 到广播端口（默认 8889）

## 温度传感器说明（Windows）

在"仪表盘→温度"卡片点"扫描温度传感器"，自动探测可用温度源，逐个勾选显示。
采集后端（无需额外安装，自动选择）：

| 后端 | 覆盖 | 权限要求 |
|---|---|---|
| nvidia-smi | NVIDIA 显卡温度（多卡逐个列出） | 无需管理员 |
| MSAcpi 热区 (WMI) | 部分主板/笔记本的 CPU 封装近似温度 | 无需管理员 |
| 存储可靠性计数器 (WMI) | 硬盘/NVMe 温度 | 部分磁盘需管理员 |
| LibreHardwareMonitor / OpenHardwareMonitor (WMI) | CPU/主板/显卡/硬盘全部真实温度 | 需以**管理员身份**运行 LHM/OHM 并保持后台运行；检测到后自动优先使用 |

提示：扫不到 CPU/主板温度是 Windows 平台限制（psutil 在 Windows 无温度 API），
以管理员身份运行 [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)
后即可获取；勾选状态保存在温度组件的 `temp_enabled`（key→bool，缺省=显示）。

## 常见问题

- **抓不到窗口/画面黑屏**：目标窗口不要最小化；UWP/管理员权限窗口需要以管理员身份运行本程序
- **画面偏移/坐标不对**：程序已设置 PerMonitorV2 DPI 感知，自定义区域请按**物理像素**填写
- **高 DPI 屏**：mss 使用物理坐标，多显示器时注意 `monitor` 序号（1 起为物理显示器，0 为虚拟合集）
- **连接不上**：确认与 ESP32 同一 Wi-Fi、IP/端口正确、PC 防火墙放行；状态栏变黄表示重连中
