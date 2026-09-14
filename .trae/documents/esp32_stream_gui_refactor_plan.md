# ESP32 Stream GUI 化重构实施计划

> 语言决策（2026-09-11 与用户确认）：对比 Python / Go(Fyne) / C# WinForms 后维持 **Python**（本项目瓶颈不在运行性能，而在 GUI 开发效率与 mss/pygetwindow/psutil 生态复用）；GUI = CustomTkinter，打包 = PyInstaller onefile+windowed；新增需求：**可最小化到系统托盘**。

## 一、仓库调研结论

### 现状（`tools/Python-ESP32-Stream/`）

PC 端主动 TCP 连接 ESP32，把画面以 RGB565 差分块推流到 TFT 屏。现有文件：

* [client.py](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/client.py)：console 入口，写死只加载第一个 pipeline，Ctrl+C 退出。

* [config\_loader.py](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/config_loader.py)：YAML 合并默认值/共享配置。

* [pipeline.py](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/pipeline.py)：核心。UDP 广播发现 IP、主动连接 ESP32、生成线程/消费线程、脏矩形差分+自适应阈值、RGB565 分包、心跳保活、断线重连。

* [window\_capture.py](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/window_capture.py)：pygetwindow 找窗口 + mss 截图（含俄文注释/调试 print）。

* [view\_window\_title.py](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/view_window_title.py)：console 下列举窗口的辅助脚本。

* [config.yaml](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/config.yaml)：当前仅一个 WINDOW\_CAPTURE pipeline，320×170，配置 IP `10.45.1.9:8888` + UDP 广播覆盖。

### 固件协议约束（必须零改动兼容）

[StreamingPlayerPage.cpp](file:///c:/Users/qtc/Documents/PlatformIO/Projects/esp32_time_weather/src/StreamingPlayerPage.cpp#L232-L247) 解析：

* 包头 12 字节：`struct.pack('!HHHH I', x, y, w, h, data_len)`；

* 数据：小端 RGB565，每包 ≤ ESP32 `MAX_CHUNK_SIZE`（现配置 8192）；

* 心跳：`x=0xFFFF, y=0xFFFF, data_len=0`；

* ESP32 为 TCP server，PC 为 client，`TCP_NODELAY`，支持断线重连。

### 现有问题（重构要解决的）

1. 只有 console，切换模式需改 yaml 重启；无法枚举窗口、无法调参数。
2. 不存在"仪表盘"模式（README 提及的 CPU/Prometheus 生成器在此精简拷贝中已不存在，需新建）。
3. RGB565 转换逐像素 Python 双重循环（每 chunk 数万次），且引入 **numba**（打包体积增加 60MB+、冷启动慢）。
4. 配置路径写死相对路径，onefile 打包后会落到 `_MEIPASS` 临时目录，配置无法持久化。
5. print 直出 stdout；`--windowed` 模式下全部丢失，异常无提示。

### 语言选型结论

**继续使用 Python**。理由：mss/Pillow/numpy/pygetwindow/psutil 生态成熟、现有代码可复用、PyInstaller 可直接产出 Windows exe；换 C#/C++ 重写收益为负。GUI 采用 **CustomTkinter**（已确认），打包 **PyInstaller --onefile --windowed**（已确认），仪表盘在 GUI 内编辑并同步 config.yaml（已确认）。

## 二、目标形态

1. 纯 GUI 应用（无控制台黑窗），三种画面源**运行时一键热切换**（不断开 ESP32 连接，切完发一帧全量刷新）：

   * **桌面**：选择显示器（mss 物理显示器枚举），可选全屏或自定义区域，裁剪对齐方式；

   * **指定窗口**：GUI 内刷新/搜索窗口列表（标题+尺寸），选中即推，裁剪对齐；

   * **仪表盘**：psutil 本地指标，Pillow 渲染到 320×170（或配置分辨率）；组件可勾选启用、上移/下移排序、改颜色、选磁盘分区/网卡；GUI 内 2× 预览。
2. 交付物：`ESP32Stream.exe` 单文件 + 同目录外部配置 `config.yaml`（首次运行自动生成默认文件，修改后持久化）+ `logs/app.log`。
3. GUI 可调连接参数（IP/端口/广播开关/分辨率/目标 FPS/伽马/白平衡等），状态栏显示连接状态、实时 FPS、帧计数；日志面板替代 console。
4. **系统托盘驻留**：关闭窗口默认隐藏到托盘（推流不中断），托盘菜单可启停推流、热切换三种画面源、查看连接状态（图标变色），"退出"才真正结束进程；可配置启动即最小化。

## 三、文件与模块

在 `tools/Python-ESP32-Stream/` 下重构为模块化包（旧 `client.py`/`view_window_title.py`/`config_loader.py` 删除，逻辑迁入新模块）：

```
Python-ESP32-Stream/
├─ app/
│  ├─ main.py                 # 入口：DPI awareness、全局 excepthook、启动 GUI
│  ├─ paths.py                # frozen/开发两种模式下的 exe 同目录路径解析
│  ├─ app_config.py           # 配置 dataclass 模型 + 默认值 + yaml 加载/保存/校验
│  ├─ logging_setup.py        # logging：队列 → GUI 日志面板 + logs/app.log 轮转
│  ├─ controller.py           # StreamController：start/stop/热切换，线程安全门面
│  ├─ tray.py                 # 系统托盘（pystray）：菜单/双击还原/状态图标/退出
│  ├─ pipeline.py             # 重构：连接管理/gen/consumer 线程，协议保持不变
│  ├─ discovery.py            # UDP 广播监听（从 pipeline.py 拆出）
│  ├─ imaging.py              # 向量化：gamma/WB、脏矩形差分、RGB565、分包
│  ├─ sources/
│  │  ├─ base.py              # FrameSource 抽象：open()/draw_frame(canvas)/close()
│  │  ├─ desktop_source.py    # mss 桌面/显示器/区域捕获
│  │  ├─ window_source.py     # pygetwindow 枚举 + mss 窗口捕获
│  │  ├─ dashboard_source.py  # 仪表盘帧源：组件调度/历史数据/布局渲染
│  │  └─ widgets.py           # clock/cpu/memory/disk/net/uptime/ip/custom_text
│  └─ gui/
│     ├─ main_window.py       # 主窗口、布局、线程↔GUI 队列泵（root.after）
│     ├─ connection_frame.py  # IP/端口/广播/分辨率/FPS/伽马/WB 设置
│     ├─ desktop_panel.py     # 显示器下拉+区域+对齐+预览
│     ├─ window_panel.py      # 窗口列表刷新/搜索/选中+对齐+预览
│     ├─ dashboard_panel.py   # 组件启用/排序/颜色/参数编辑 + 2× 预览
│     ├─ status_bar.py        # 开始/停止、连接状态灯、FPS、帧计数、重连提示
│     └─ log_view.py          # 环形日志文本框
├─ tools/
│  └─ fake_esp32_receiver.py  # 开发自检：TCP 收包校验协议（不打包）
├─ build/
│  ├─ esp32_stream.spec       # PyInstaller 规格（onefile/windowed/collect-data）
│  └─ build.ps1               # 一键打包脚本
├─ config.yaml                # 运行时外部配置（首次自动生成）
├─ requirements.txt           # 运行依赖 + 注释说明构建依赖
└─ README.md                  # 更新：GUI 使用/配置/打包说明（仅更新现有文件）
```

### 配置文件结构（config.yaml）

```yaml
connection:
  esp32_host: "10.45.1.9"
  esp32_port: 8888
  use_broadcast: true
  broadcast_port: 8889
  broadcast_hold_time: 30
  reconnect_interval_sec: 3.0
  socket_timeout: 1.0
video:
  target_width: 320
  target_height: 170
  target_fps: 24.0
  generator_target_interval_sec: 0.03
  max_chunk_data_size: 8192
  gamma: 1.2
  wb_scale: [1.1, 1.05, 0.95]
  min/max_dirty_rect_threshold、step_up/down、fps_history_size 等沿用现配置
  frames_queue_max_size、heartbeat_interval_sec 沿用
active_source: "window"        # desktop | window | dashboard
ui:
  close_to_tray: true          # 点窗口 X 最小化到托盘而非退出（托盘菜单"退出"才真正结束）
  start_minimized: false       # 启动时直接隐藏到托盘
  tray_switch_source: true     # 托盘菜单允许快速切换三种画面源
sources:
  desktop: {monitor: 0, region: null, crop_alignment: "center"}
  window:  {window_title: "任务管理器", crop_alignment: "left"}
  dashboard:
    background: "#0B0F14"
    refresh_interval_sec: 1.0
    widgets:
      - {type: "clock",     enabled: true,  color: "#FFD700"}
      - {type: "cpu",       enabled: true,  color: "#00E676", sparkline: true}
      - {type: "memory",    enabled: true,  color: "#40C4FF"}
      - {type: "disk",      enabled: true,  color: "#FFAB40", path: "C:\\"}
      - {type: "net",       enabled: true,  color: "#E040FB", adapter: null}
      - {type: "uptime",    enabled: false, color: "#FFFFFF"}
      - {type: "ip",        enabled: false, color: "#AAAAAA"}
      - {type: "custom_text", enabled: false, color: "#FFFFFF", text: ""}
```

## 四、关键技术设计

1. **imaging.py 全 numpy 向量化，移除 numba**

   * RGB565：`((R&0xF8)<<8)|((G&0xFC)<<3)|(B>>3)` 整张数组一次算出，`.astype('<u2').tobytes()`；

   * gamma/白平衡、差分（`np.sum(abs(...),axis=2) > threshold`）均为数组操作；

   * 320×170 全帧转换 <2ms，exe 体积预计从 150MB+ 降到 40–60MB。

2. **热切换（不断 TCP）**

   * `StreamController.switch_source(mode, params)` 加锁：停 generator 线程 → 替换 `FrameSource`（open/close）→ 清空帧队列 → consumer 置 `prev=None`（下一帧自动全量脏矩形）→ 启新 generator；

   * 连接参数（IP/端口/分辨率等）变更则触发完整 session 重连（复用现有 reconnect 循环，每次连接前重读 config 快照）。

3. **线程模型**：manager 连接线程 + generator + consumer + 广播监听，全部 daemon；工作线程**绝不触碰 tk 控件**，状态/日志入 `queue.Queue`，主线程 `root.after(200ms)` 泵取刷新。

4. **外部配置路径**：

   ```python
   APP_DIR = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[1]
   ```

   config.yaml / logs/ 一律相对 `APP_DIR`；缺失则写默认配置。

5. **无 console 适配**：所有 print 改 logging；`--windowed` 下 stdout 为 None 不崩溃；`sys.excepthook`/线程异常捕获 → messagebox + 文件日志。

6. **仪表盘组件**（widgets.py，均输出 PIL 绘制指令）：

   * `clock` 日期时间；`cpu` 总占用（条+可选 sparkline 历史曲线，`psutil.cpu_percent` 首次 prime）；`memory` 已用/总量/百分比；`disk` 分区选择（`disk_partitions`+`disk_usage`）；`net` 上下行速率（`net_io_counters` 差分，B/s 自适应单位，可选网卡）；`uptime`；`ip`（本机 IP/主机名）；`custom_text`。

   * 布局：单列纵向，按启用顺序累加 y；组件超出画布高度时在编辑器/预览中给出提示。

   * dashboard\_source 按 `refresh_interval_sec` 采集（与推流帧率解耦，值不变时重绘同一帧，差分逻辑天然抑制流量）。

7. **GUI 交互**

   * 主窗口：顶部连接设置；中部 `CTkTabview` 三个源面板（切换 tab 即"待切换模式"，点"开始推流"或运行中点"应用切换"立即生效）；底部状态栏 + 日志折叠面板。

   * 窗口面板：`刷新`按钮调 pygetwindow 枚举（过滤空标题，显示标题/尺寸），搜索框过滤，选中项高亮；窗口不存在时状态提示并自动回退等待。

   * 仪表盘面板：`CTkScrollableFrame` 组件卡片列表（启用 checkbox、上移/下移、颜色按钮调 `tkinter.colorchooser`、disk 分区下拉、net 网卡下拉、sparkline 开关）；右侧 2× 预览（PIL→CTkImage，after 定时刷新）；保存即写 config.yaml。

   * 桌面/窗口面板同样提供 2× 静态/实时预览。

8. **系统托盘（tray.py，新增需求）**

   * 库选型（按项目规范列备选）：**pystray（采用）**——跨平台、Windows 走原生 Win32 后端、菜单/双击/气泡通知 API 完整，与 PIL 图标无缝配合；备选 `infi.systray`（更轻但久未维护、无动态菜单能力）、pywin32 手写 Shell\_NotifyIcon（零新依赖但样板代码多、跨平台无望）。

   * 行为：点窗口关闭按钮（X）拦截为隐藏到托盘（`close_to_tray=true` 时），首次隐藏弹一次气泡"程序仍在后台运行"；双击托盘图标还原窗口；`start_minimized=true` 时启动即驻留托盘。

   * 托盘菜单：`显示主界面` / 分隔 / `开始推流`·`停止推流` / `画面源 ▶ 桌面·指定窗口·仪表盘`（当前模式打勾，点击即热切换）/ 分隔 / `退出`（唯一真正退出入口：停推流、停 pipeline、停 tk 主循环、移除托盘图标）。

   * 状态图标：Pillow 按连接状态动态生成 16/32px 圆点图标（灰=未连接、黄=重连中、绿=已连接），状态队列泵刷新时调 `icon.icon=` 热替换；tooltip 显示"ESP32 Stream — {状态} · {FPS}fps"。

   * 线程边界：pystray 在自己的线程跑 Win32 消息循环；菜单回调**绝不直接操作 tk**，只往现有 GUI 事件队列投递命令（show/quit/start/stop/switch），由主线程 `root.after` 泵统一执行；退出时 `icon.stop()` 与 `root.destroy()` 幂等互防重复。

9. **打包（build/esp32\_stream.spec）**

   * `onefile` + `windowed` + `name=ESP32Stream` + icon（如无图标资源则省略）；

   * `--collect-data customtkinter`（主题 json 必须打入）；hiddenimport：`mss.windows`、`PIL._tkinter_finder`、`pystray._win32`；

   * 排除 numba/llvmlite/test 模块减小体积；

   * build.ps1 先 `pip install pyinstaller` 再调用 spec（PowerShell 用 `;` 分隔，不用 `&&`）。

## 五、实施步骤（依赖顺序）

1. 搭骨架：目录、`paths.py`、`app_config.py`（dataclass+默认 yaml+校验+保存）、`logging_setup.py`。
2. `imaging.py`：向量化色彩校正/差分/RGB565/分包，用 `tools/fake_esp32_receiver.py` 思路写本地断言自检（与旧实现逐字节比对）。
3. `sources/base.py` + `desktop_source.py` + `window_source.py`：移植 mss/pygetwindow，清理俄文注释与 print，窗口枚举函数供 GUI 复用。
4. `sources/widgets.py` + `dashboard_source.py`：psutil 采集、sparkline、布局渲染，PIL 直接出图目检。
5. `discovery.py` + `pipeline.py` 重构：统一 FrameSource 接口、配置快照、热切换钩子；协议字节保持不变。
6. `controller.py`：start/stop/apply\_config/switch\_source + 状态队列（连接状态/FPS/帧计数/日志）。
7. GUI：main\_window 骨架 → connection\_frame → 三个源面板 → status\_bar → log\_view → 队列泵。
8. `tray.py` 系统托盘：图标状态机、菜单（含画面源热切换）、X 拦截隐藏、双击还原、气泡提示，回调全部走 GUI 事件队列。
9. 仪表盘编辑器 + 三源 2× 预览；配置保存/重载；异常钩子、DPI awareness（入口调 shcore SetProcessDpiAwareness，保证 mss 区域与屏幕坐标一致）。
10. 端到端联调：开发模式 `python -m app.main`，三模式切换/断网重连/配置持久化/托盘驻留与退出。
11. PyInstaller 打包：spec + build.ps1；拷贝到干净目录运行，验证：无控制台、config.yaml 自动生成且可持久化、三模式可用、托盘可用、日志文件生成。
12. 更新 README 使用/打包说明；真机 ESP32 验证由用户执行（附验证清单）。

## 六、依赖

* 运行：`customtkinter`、`mss`、`pillow`、`numpy`、`pyyaml`、`pygetwindow`（Windows 自动带 pyrect）、`psutil`、`pystray`（托盘，备选见设计 §8）；

* 构建：`pyinstaller`；

* **移除**：`numba`；Python 3.10+（代码用 `X | None` 注解），与用户现有 Python 3.12 环境一致。

* 代码规范遵循项目 CLAUDE.md：Python 沿用既有 4 空格/snake\_case 风格（与现有文件一致），单文件 ≤300 行、单函数 ≤30 行（模块切分已按此约束规划），关键逻辑中文注释。

## 七、验证

* 单元级：imaging 输出与旧 numba 版逐字节一致；fake\_esp32\_receiver.py 收到合法 12 字节头、心跳包、分包 data\_len 正确。

* 集成级（开发模式）：

  * 桌面/窗口/仪表盘三种源切换不重启程序、TCP 不断开（fake receiver 观察连接保持）、切换后首帧为全量；

  * 窗口枚举/搜索/选中不存在窗口时的回退提示；

  * 仪表盘增删/排序/改色后预览即时变化，重启程序配置恢复；

  * 改 IP/端口后自动重连到新目标；杀网络/拔 ESP32 后按间隔重连。

  * 托盘：点 X 隐藏到托盘且推流不中断；双击/菜单还原；托盘菜单启停与画面源热切换生效；图标随连接状态变色、tooltip FPS 刷新；托盘"退出"后进程、端口、线程全部干净结束（任务管理器无残留）。

* 打包级（干净目录）：单 exe 双击无黑窗；config.yaml 与 logs/ 落在 exe 同目录；任务管理器确认无 conhost 子进程；托盘图标正常显示；体积目标 ≤60MB。

* 真机级（用户）：ESP32 收到桌面、指定窗口、仪表盘三画面，热切换无花屏（首帧全量保证）。

## 八、风险与应对

* **onefile 冷启动慢/杀软误报**：spec 同步保留 onedir 注释配置，必要时一条命令切 onedir。

* **customtkinter 主题打包遗漏导致白底崩溃**：spec 显式 `collect_data_files('customtkinter')`，干净目录先行验证。

* **最小化窗口/管理员窗口/UWP 抓不到或黑屏**：窗口列表标注状态；文档提示需窗口非最小化；以管理员运行 exe 可抓高权限窗口（README 说明）。

* **高 DPI 下区域坐标错位**：入口设置 PerMonitorV2 DPI awareness，mss 用物理坐标，GUI 区域输入标注物理像素。

* **widget 总高超 170**：编辑器与预览给出"超出画布"红色提示，不静默裁剪。

* **windowed 模式异常无感知**：全局 excepthook + threading 兜底 → messagebox + app.log。

* **托盘跨线程操作 tk 崩溃**：pystray 回调只投递事件队列，主线程 after 泵执行；退出路径加幂等标志，防 icon.stop()/destroy() 重入。

* **pystray 后端打包遗漏导致托盘不显示**：spec hiddenimport 显式加 `pystray._win32`，干净目录验证托盘图标与菜单。

