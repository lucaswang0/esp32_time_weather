# esp32-host-go: ESP32-C3 TFT 串流上位机（Go 重构方案）

> 将 `tools/Python-ESP32-Stream` 重构为 Go 实现的 PC 端上位机 GUI 程序。
> 目标：完全兼容现有 ESP32 固件 (`StreamingPlayerPage.cpp`) 的串流协议；
> Windows 平台优先；多 pipeline 并行；系统信息源支持鼠标拖动布局。

***

## 1. 目标与非目标

### 1.1 目标

| #  | 项              | 描述                                                                              |
| -- | -------------- | ------------------------------------------------------------------------------- |
| G1 | **协议兼容**       | 100% 复用 ESP32 现有 12 字节头 + RGB565 协议、UDP 广播 `ESP32:<ip>:<port>`、TCP 主动连接 `:8888` |
| G2 | **多数据源**       | 同时实现 ① 桌面截屏 ② 窗口截屏 ③ 系统性能信息（CPU/Mem/Net/Disk/...）                               |
| G3 | **GUI 应用**     | Fyne 编写；多 pipeline 列表；系统信息源支持**鼠标拖动 widget 坐标**                                 |
| G4 | **系统性能信息**     | 支持配置多套显示方案，可以按需切换。                                                              |
| G5 | **Windows 优先** | 跨平台代码结构，但 Windows 优先适配，macOS/Linux 留 stub                                       |
| G6 | **零配置发现**      | 监听 `8889` UDP 广播，自动获得 ESP32 IP/Port（保留配置 `esp32.host` 兜底）                       |

### 1.2 非目标（本期不做）

- 不修改 ESP32 固件；`StreamingPlayerPage.cpp` / `.h` 保持原样
- 不引入外部 Prometheus 服务（系统信息源直接用 `gopsutil` 本机采集）
- 不实现压缩 / RLE / dithering（沿用 Python 版的全 RGB565 直传 + 脏矩形差分）
- 不做远程 HTTP 控制接口（GUI + YAML 足够）

***

## 2. 现有代码事实（重构依据）

- **Python 现有文件** ([Python-ESP32-Stream/](file:///C:/Users/user/Documents/PlatformIO/Projects/esp32_time_weather/tools/Python-ESP32-Stream/))：
  - `client.py`（入口）
  - `pipeline.py`（核心流：Source→Queue→Diff→RGB565→Chunk→Send）
  - `window_capture.py`（pygetwindow + mss）
  - `config_loader.py`（YAML）
  - `view_window_title.py`（窗口枚举小工具）
  - `config.yaml`、`requirements.txt`、`README.md`
  - 残留 `.pyc` 缓存暗示曾有 `bios_drawer.py` / `cpu_monitor_generator.py` / `prometheus_monitor_generator.py` / `metrics.py` / `graphics_engine.py` / `server.py`，但 `.py` 已被删除——这些是"系统性能信息"模式的历史参考，不直接复用。
- **ESP32 固件协议常量**（`include/StreamingPlayerPage.h`）：
  - `SERVER_PORT = 8888`（TCP，被 PC 主动 connect）
  - `BROADCAST_PORT = 8889`（UDP，ESP32 周期广播 `ESP32:<ip>:<port>`，间隔 2000 ms）
  - `HEADER_SIZE = 12`、`MAX_CHUNK_SIZE = 8192`、`BUFFER_SIZE = HEADER_SIZE + MAX_CHUNK_SIZE`
  - 屏幕：`ST7789 170×320`（注意在 `platformio.ini` 中 `TFT_WIDTH=170, TFT_HEIGHT=320`），即宽 170 / 高 320
  - 头部解包使用大端；像素 `setSwapBytes(true)` 读取 → 每像素 uint16 在网络上是 **little-endian**
- **现有 Python pipeline 行为**（要复刻的逻辑）：
  - 帧生成线程 → `frames_queue`（maxsize=8）→ 消费线程
  - 消费线程：缩放 → 脏矩形（曼哈顿距离，自适应阈值）→ RGB565 → 按行 chunk → sendall
  - 静止时发送心跳包 `[0xFFFF, 0xFFFF, 0, 0, 0]`（header 12B）
  - 自适应阈值：FPS < target → threshold 上升；FPS > target → threshold 下降
  - 主动连接 ESP32，失败 `reconnect_interval_sec` 后重试

***

## 3. 目录与模块

新建 `tools/esp32-host-go/`，与原 Python 目录并列。Python 目录暂留作参考，待 Go 版验证后由用户决定是否删除。

```
tools/esp32-host-go/
├── go.mod                          // module esp32_host
├── go.sum
├── README.md
├── config.example.yaml
├── main.go                         // fyne.App 入口
├── internal/
│   ├── config/
│   │   └── config.go               // YAML 加载/保存/校验
│   ├── protocol/
│   │   ├── header.go               // 12B 头 big-endian 打包/解包
│   │   ├── rgb565.go               // image.Image → RGB565 LE bytes
│   │   └── chunk.go                // 矩形按行切分到 8KB 包
│   ├── source/
│   │   ├── source.go               // type Source interface
│   │   ├── screen.go               // 桌面：kbinani/screenshot
│   │   ├── window.go               // 窗口：lxn/win 枚举 + 截屏（Windows）
│   │   ├── window_stub.go          // 非 Windows 平台 stub
│   │   ├── sysinfo.go              // gopsutil + fogleman/gg 渲染
│   │   └── widgets.go              // CPU/Mem/Net/Disk/Text widget 类型
│   ├── pipeline/
│   │   ├── pipeline.go             // gen → diff → encode → send
│   │   ├── diff.go                 // 脏矩形 + 自适应阈值
│   │   └── color.go                // gamma + wb_scale numpy 等价
│   ├── transport/
│   │   ├── esp32.go                // 主动连接 ESP32:8888，TCP_NODELAY
│   │   └── discovery.go            // 监听 8889 UDP 广播
│   ├── metrics/
│   │   └── metrics.go              // 内部 FPS/队列/阈值指标（仅内存，结构体）
│   └── ui/
│       ├── app.go                  // Fyne 主窗口
│       ├── pipeline_list.go        // 左侧 pipeline 列表 + 启停
│       ├── pipeline_editor.go      // 中部 pipeline 配置表单
│       ├── sysinfo_canvas.go       // 系统信息可拖动布局画布
│       ├── widget_palette.go       // 可添加的 widget 类型面板
│       ├── log_view.go             // 底部日志
│       └── statusbar.go            // 状态栏 + 系统托盘入口
└── scripts/
    ├── build.ps1                   // Windows 编译脚本
    └── dev_run.ps1                 // 本地热跑
```

***

## 4. 核心抽象

### 4.1 `Source` 接口

```go
type Source interface {
    Resolution() (w, h int)
    Next(ctx context.Context) (image.Image, error)   // 返回已缩放到目标分辨率的帧
    Close() error
}
```

- `screen.Source` / `window.Source` / `sysinfo.Source` 三个实现
- 每个 pipeline 持有自己的 `Source` 实例

### 4.2 `Pipeline` 编排

```go
type Pipeline struct {
    cfg          config.PipelineConfig
    source       source.Source
    transport    *transport.ESP32Client
    frameCh      chan image.Image      // buffered, maxsize=8
    cancel       context.CancelFunc
    metrics      *metrics.PipelineMetrics
}
```

- Goroutine A: `source.Next() → frameCh`
- Goroutine B: 读 `frameCh` → resize → diff → RGB565 → chunk → `transport.Send()`
- 静止帧：每 `heartbeat_interval_sec` 发心跳包
- 自适应阈值：消费循环窗口期调整 `min ≤ threshold ≤ max`
- 启动 / 停止通过 `context.Context` 取消

### 4.3 `transport.ESP32Client`

- UDP 监听 `8889`（goroutine）→ 解析 `ESP32:<ip>:<port>` → 写 `lastBroadcastIP/Port`
- TCP `net.Dial("tcp", ip:port)` 主动连接，启用 `TCP_NODELAY`
- 连接失败：`reconnect_interval_sec` 退避重试
- 配置 `esp32.host` 非空 → 直连；否则等待广播 IP 出现且 `broadcast_hold_time` 内有效 → 用之
- 写操作加 `sync.Mutex`（因为心跳和脏矩形共用一条 TCP）

### 4.4 协议细节

- **Header**（12B，big-endian）：`X u16 | Y u16 | W u16 | H u16 | DataLen u32`
- **Body**：`DataLen` 字节 RGB565，每像素 `uint16` little-endian
- **Heartbeat**：X=0xFFFF Y=0xFFFF W=0 H=0 DataLen=0
- **分块**：`bytesPerRow = W*2`；`chunkH = max(1, maxChunkData / bytesPerRow)`（Python 版等价实现）
- 头部打包用 `encoding/binary.BigEndian`，像素用 `binary.LittleEndian.PutUint16`

***

## 5. 数据源实现要点

### 5.1 桌面截屏（`source/screen.go`）

- 库：`github.com/kbinani/screenshot`（纯 Go，跨平台，CGo-free）
- 配置：`monitor`（int）选择显示器，`region {top,left,width,height}` 可选子区域
- 缩放：`golang.org/x/image/draw.Kernel.Scale` 到目标分辨率

### 5.2 窗口截屏（`source/window.go`）

- Windows：`github.com/lxn/win`（purego 调用 Win32 API）
  - `EnumWindows` + `GetWindowTextW` 枚举可见窗口
  - 用户在 GUI 下拉框选窗口（或 YAML 填 `window_title`）
  - `GetWindowRect` 取 bounds → `kbinani/screenshot.CaptureRect`
- macOS/Linux：仅提供 stub（`window_stub.go`）并返回 `errNotSupported`，UI 上标灰
- 缩放比例与原 Python 相同：根据 `crop_alignment`（left/center/right）裁切后再 LANCZOS 缩放

### 5.3 系统性能信息（`source/sysinfo.go` + `widgets.go`）

- 采集：`github.com/shirou/gopsutil/v3`（CPU/Mem/Swap/Net/Disk/Host/Uptime）
- 渲染：`github.com/fogleman/gg` 在 `*gg.Context` 上画仪表盘
- **Widget 类型**（实现 `Widget` 接口）：
  - `cpu`（条形图 + 百分比 + 频率）
  - `mem`（条形图 + used/total）
  - `swap`（条形图）
  - `net`（上行/下行数字 + 最近 30s sparkline）
  - `disk`（按 drive 列表）
  - `host`（hostname / OS / uptime 文本）
  - `text`（用户自定义文本 / 标签）
- **可拖动布局**：
  - Fyne 容器 `NewContainerWithoutLayout(...)`
  - 每个 widget 是 `DraggableRect`，鼠标按下 → 记录偏移 → 拖动 → 释放时把 (X,Y) 写回 `PipelineConfig.Source.SysInfo.Widgets[i]`
  - 双击 widget → 弹出属性编辑（文字、颜色、字体大小）
  - 调色板（左/右栏）→ 拖入画布新增 widget

### 5.4 三个源共同点

- 输出 `image.Image`，`Resolution()` 始终 = 目标 TFT 宽高（170×320 或 320×170，按配置 `target.rotate` 旋转）
- 不在 source 阶段做 gamma / wb\_scale（统一放到 `pipeline.color.go`）

***

## 6. GUI 设计（Fyne）

### 6.1 主窗口布局

```
┌────────────────────────────────────────────────────────────┐
│  [ + 新建 Pipeline ]   [ 保存配置 ]   [ 启动全部 ] [ 停止全部 ]  │
├──────────────┬─────────────────────────────────────────────┤
│ Pipelines    │   Pipeline 编辑器（Tab：基本 / 源 / 颜色 / 高级）│
│ ┌──────────┐ │  ┌─────────────────────────────────────────┐│
│ │ MainScn  │ │  │ 名称: [MainScreen]                        ││
│ │ ▶ running│ │  │ 目标: W=170 H=320 (竖屏)                  ││
│ ├──────────┤ │  │ ESP32: [自动发现▼]  端口: [8888]            ││
│ │ SysInfo  │ │  │ 源类型: [屏幕截屏▼]                         ││
│ │ ⏸ paused │ │  │ 显示器: [主显示器▼]                        ││
│ ├──────────┤ │  │ 目标 FPS: [24]  阈值范围: [1] - [180]        ││
│ │ WinCap   │ │  │ Gamma: [1.2]  白平衡: [1.1,1.05,0.95]      ││
│ │ ⏸ paused │ │  └─────────────────────────────────────────┘│
│ └──────────┘ │  [ 启动 ] [ 停止 ] [ 应用 ]                     │
├──────────────┴─────────────────────────────────────────────┤
│ 日志:                                                    │
│ [12:00:01] [MainScn] 找到 ESP32 10.45.1.9:8888             │
│ [12:00:02] [MainScn] 已连接                                │
│ [12:00:03] [MainScn] FPS=23.8 thr=12                       │
├────────────────────────────────────────────────────────────┤
│ 状态: 2 个 pipeline 运行中 | 0 错误 | 系统托盘: 启用         │
└────────────────────────────────────────────────────────────┘
```

- 选中 `SysInfo` pipeline 时，主区域切到"调色板 + 画布"双栏：
  - 左：可拖入的 widget 类型（CPU / Mem / Net / Disk / Text / Host）
  - 右：`NewContainerWithoutLayout` 的可拖动画布
  - 工具栏：\[添加文字] \[清空] \[网格对齐] \[保存布局]

### 6.2 Fyne 关键 API

- `app.New()` / `app.NewWindow()`
- `widget.NewCard` / `widget.NewForm` 做表单
- `container.NewBorder` / `container.NewHSplit` / `container.NewVSplit` 布局
- `NewContainerWithoutLayout` + 自定义 `DraggableRect` 画可拖动 widget
- `fyne.CurrentApp().SendNotification` 失败/重连提示
- 系统托盘：`fyne.io/fyne/v2` 不直接提供；使用 `fyne.io/systray`（同作者、独立包）做最小化托盘

***

## 7. YAML 配置 schema（`config.example.yaml`）

```yaml
global:
  log_level: info            # debug/info/warn/error
  udp_broadcast_port: 8889
  reconnect_interval_sec: 3.0
  heartbeat_interval_sec: 2.0

pipelines:
  - name: "MainScreen"
    enabled: true
    esp32:
      host: ""               # 留空 = 自动发现
      port: 8888
      use_broadcast: true
      broadcast_hold_time: 30
    target:
      width: 170
      height: 320
    source:
      type: screen           # screen | window | sysinfo
      screen:
        monitor: 0
        # region: {top: 0, left: 0, width: 800, height: 600}
    diff:
      target_fps: 24
      min_threshold: 1
      max_threshold: 180
      step_up: 15
      step_down: 5
      hysteresis: 0.03
    colors:
      gamma: 1.2
      wb_scale: [1.1, 1.05, 0.95]

  - name: "SysInfoPanel"
    enabled: false
    esp32:
      host: ""
      port: 8888
      use_broadcast: true
      broadcast_hold_time: 30
    target:
      width: 170
      height: 320
    source:
      type: sysinfo
      sysinfo:
        refresh_ms: 500
        widgets:
          - type: cpu
            x: 5  y: 5   w: 160  h: 50
          - type: mem
            x: 5  y: 60  w: 160  h: 50
          - type: net
            x: 5  y: 115 w: 160  h: 50
          - type: text
            x: 5  y: 170 w: 160  h: 40
            text: "ESP32-C3"
            font_size: 14
            color: [200, 200, 200]
```

***

## 8. 第三方依赖（`go.mod`）

| 包                               | 用途               | 备注                               |
| ------------------------------- | ---------------- | -------------------------------- |
| `fyne.io/fyne/v2`               | GUI              | 纯 Go，无 CGo                       |
| `fyne.io/systray`               | 系统托盘             | 同作者                              |
| `github.com/kbinani/screenshot` | 桌面/窗口截屏          | 纯 Go                             |
| `github.com/lxn/win`            | Win32 API（窗口枚举）  | 仅 Windows，用 `//go:build windows` |
| `github.com/shirou/gopsutil/v3` | CPU/Mem/Net/Disk | 跨平台                              |
| `github.com/fogleman/gg`        | 2D 绘图（仪表盘）       | 纯 Go                             |
| `gopkg.in/yaml.v3`              | YAML             | <br />                           |
| `golang.org/x/image`            | 缩放 / 字体          | <br />                           |

故意避免：

- `github.com/go-vgo/robotgo`（CGo 依赖重，Windows 编译链脆弱；改用 `lxn/win` 直接调 Win32）
- `github.com/disintegration/imaging`（已被 `x/image/draw` 取代）

***

## 9. 构建与运行

```powershell
# 一次性
cd tools\esp32-host-go
go mod init esp32_host
go mod tidy

# 开发热跑
fyne run                              # 或 go run .

# 跨平台编译
$env:GOOS="windows"; $env:GOARCH="amd64"
go build -trimpath -ldflags "-s -w" -o dist\esp32-host-go.exe .
# 可选：fyne package -os windows -icon icon.png → 打包为 .exe
```

- 默认零配置：首次启动若无 `config.yaml`，写入 `config.example.yaml` 副本到用户数据目录
- 日志：内存 ring buffer + 可选文件（`%APPDATA%/esp32-host-go/app.log`）

***

## 10. 迁移路径

| 阶段     | 动作                                                       | 验证                                         |
| ------ | -------------------------------------------------------- | ------------------------------------------ |
| **M1** | 初始化 Go 模块、目录结构、`config` + `protocol` + `transport` 三个核心包 | 单元测试：12B 头打包解包、RGB565 转换与 Python 版字节级一致    |
| **M2** | `pipeline` + `source/screen` + 自动发现 + 差分 + 阈值自适应         | 用现有 ESP32 板子跑通：桌面截屏 → 320×170 屏正常显示        |
| **M3** | `source/window`（lxn/win）                                 | 选中"任务管理器"窗口 → 实时显示在屏幕上                     |
| **M4** | `source/sysinfo` + `widgets.go`                          | 仪表盘实时显示 CPU/Mem/Net                        |
| **M5** | Fyne GUI：pipeline 列表、配置表单、日志、状态栏                         | 全部可点可改可保存                                  |
| **M6** | 系统信息可拖动布局（核心交互）                                          | 在画布上拖动 CPU widget → 松开后 YAML 自动更新 → 重启位置保留 |
| **M7** | 系统托盘 + 通知 + 多 pipeline 并行                                | 启停不卡；多 pipeline 各自独立 FPS 显示                |
| **M8** | 清理：删除原 `Python-ESP32-Stream/`、更新项目 README 指向 Go 版        | `tools/` 目录清爽                              |

每阶段结束都用现有 ESP32 板子做端到端验证（Python-ESP32-Stream 仍可作为对照）。

***

## 11. 风险与权衡

| 风险                                 | 影响     | 缓解                                                                 |
| ---------------------------------- | ------ | ------------------------------------------------------------------ |
| Fyne 自定义拖动布局工作量大                   | M6 延期  | M6 单独留 1–2 天余量；若超期可降级为"数值输入坐标"                                     |
| `lxn/win` 在新 Windows SDK 下兼容性      | M3 不可用 | 备选：直接通过 PowerShell `Get-Process` + `MainWindowHandle` 拿 HWND，进程外通信 |
| Go GC 偶发暂停影响低延迟                    | 帧间隔抖动  | `-gcflags="-m"` 调优；帧生成/发送缓冲 8 帧可吸收；用 `runtime.GOMAXPROCS(1)` 隔离大对象 |
| Fyne 主题在中文字体上发虚                    | 视觉问题   | M5 阶段确认：必要时引入 `sourcehan` 或 `wenquanyi` 字体文件                       |
| 多 ESP32 + 多 pipeline 抢同一 broadcast | 误连     | 严格按 `lastBroadcastIP/Port` 绑定 pipeline；UI 显示当前绑定的 ESP32 IP         |

***

## 12. 文件清单（创建/修改）

| 路径                                        | 动作                                |
| ----------------------------------------- | --------------------------------- |
| `tools/esp32-host-go/`                    | 新建（go.mod, main.go, internal/...） |
| `tools/esp32-host-go/config.example.yaml` | 新建                                |
| `tools/esp32-host-go/README.md`           | 新建                                |
| `tools/esp32-host-go/scripts/build.ps1`   | 新建                                |
| `tools/Python-ESP32-Stream/`              | 暂保留（M8 后由用户决定删/留）                 |
| `README.md`（项目根）                          | 末尾追加一段"PC 端串流上位机"指向 Go 版          |
| `include/StreamingPlayerPage.cpp` / `.h`  | **不改**                            |

***

## 13. 验收标准

1. `go build` 在 Windows 10/11 干净环境下零警告通过
2. 单二进制 `esp32-host-go.exe` 启动后：
   - 默认 config 跑桌面截屏 → ESP32 TFT 实时显示，FPS ≥ 18
   - 切换到"系统信息"源 → CPU/Mem/Net 实时刷新
   - 拖动 CPU widget → 松手后保存到 YAML → 重启后位置保留
   - 关闭 GUI 窗口 → 系统托盘继续运行 → 托盘菜单"退出"才完全停止
3. 关闭/重启 ESP32 → GUI 状态栏 1–2 秒内显示重连，日志记录重连次数
4. 帧率自适应：把窗口最小化（无 dirty rect）→ 阈值自动升到 max；恢复 → 阈值自动降
5. 协议字节级与 Python 版一致：用脚本对比同样一张图两版打包出的前 10 个包，二进制相等

***

**计划就绪，等待确认。** 确认后即开始 M1。
