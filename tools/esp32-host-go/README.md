# esp32-host-go

PC 端 ESP32-C3 TFT 串流上位机，Go 重构版本。与 `tools/Python-ESP32-Stream` 等价，
并新增 GUI（Fyne）+ 系统性能信息源 + 可拖动 widget 布局。

## 进度

| 阶段 | 状态 | 内容 |
|---|---|---|
| M1 | ✅ 完成 | Go 模块、config / protocol / transport / metrics 核心包 + 单元测试 |
| M2 | ✅ 完成 | pipeline 编排 + source/screen + 差分/阈值 + 健壮性插桩（超时/重连/panic 恢复） |
| M3 | ✅ 完成 | source/window（Win32 窗口枚举 + 截屏 + 裁剪） |
| M4 | ✅ 完成 | source/sysinfo（gopsutil + gg 仪表盘，5 类 widget） |
| M5 | ✅ 完成 | Fyne GUI（pipeline 列表、配置表单、日志、状态栏）+ 系统托盘 |
| M6 | ⏳ 待办 | 系统信息可拖动 widget 布局 |
| M7 | ⏳ 待办 | 多 pipeline 并行完善 + 通知 |
| M8 | ⏳ 待办 | 清理旧 Python 实现 + README 更新 |

## 构建

```powershell
cd tools\esp32-host-go
go mod tidy
go build -trimpath -ldflags "-s -w" -o dist\esp32-host-go.exe .
```

或使用脚本：

```powershell
.\scripts\build.ps1
```

> Windows + Go 1.22+ 即可。Go 会按需自动下载 toolchain（首次 `go build` 时）。

## 运行

```powershell
# 默认从同目录 config.yaml / config.example.yaml 加载
.\dist\esp32-host-go.exe
```

- 加载顺序：exe 同目录 `config.yaml` → 同目录 `config.example.yaml` → 当前工作目录同名文件
- 若都不存在会报错退出
- Ctrl+C 优雅退出

### GUI 模式（默认）

```powershell
.\dist\esp32-host-go.exe
```

- 启动 Fyne GUI 应用
- 关闭窗口时最小化到系统托盘（右下角）
- 托盘菜单可显示/隐藏窗口、启停 pipeline、退出程序

### CLI 模式（无头模式）

```powershell
# 方式 1：环境变量
$env:HEADLESS=1; .\dist\esp32-host-go.exe

# 方式 2：命令行参数
.\dist\esp32-host-go.exe --cli
```

- 无 GUI，纯命令行运行
- 适合服务器/无头环境、自动化部署
- 日志输出到 stdout

**典型日志输出**（无 ESP32 板子时）：

```
INFO  loading config       path=...\config.yaml
INFO  config loaded        pipelines=1
WARN  discovery start failed ... :8889 bind conflict (port 被旧实例占用)
INFO  running              active_pipelines=1
INFO  connecting to ESP32  pipeline=MainScreen
WARN  connect failed       err="no valid ESP32 broadcast within 30s" after=1s
INFO  connecting to ESP32  ... (持续重连，指数退避 1s → 30s)
```

## 协议

与 ESP32 固件 `include/StreamingPlayerPage.h` 字节级兼容：

- TCP 端口 8888（PC 主动连接，启用 TCP_NODELAY）
- UDP 端口 8889 监听 ESP32 广播 `ESP32:<ip>:<port>`
- 头部 12 字节大端：`X u16 | Y u16 | W u16 | H u16 | DataLen u32`
- 像素：每像素 `uint16` little-endian RGB565，TFT_eSPI 端 `setSwapBytes(true)` 读取
- 心跳：`X=0xFFFF, Y=0xFFFF, W=0, H=0, DataLen=0`

## 配置（`config.yaml`）

```yaml
global:
  log_level: info            # debug | info | warn | error
  udp_broadcast_port: 8889
  reconnect_interval_sec: 3.0
  heartbeat_interval_sec: 2.0
  max_chunk_data_size: 8192
  frames_queue_max_size: 8
  generator_low_water_mark: 3

pipelines:
  - name: "MainScreen"
    enabled: true
    esp32:
      host: ""                # 留空 → 走 UDP 广播自动发现
      port: 8888
      use_broadcast: true
      broadcast_hold_time: 30
      socket_timeout_sec: 2.0
    target:
      width: 170
      height: 320
    source:
      type: screen            # screen | window | sysinfo
      screen:
        monitor: 0            # 0 = 主显示器，1+ = Display N
        # region: { top: 0, left: 0, width: 800, height: 600 }
    diff:
      target_fps: 24.0
      min_threshold: 1
      max_threshold: 180
      step_up: 15
      step_down: 5
      hysteresis: 0.03
      generator_target_interval_sec: 0.033
    colors:
      gamma: 1.2
      wb_scale: [1.1, 1.05, 0.95]
    queue:
      max_size: 8
      low_water_mark: 3

  # 窗口捕获示例
  - name: "BrowserWindow"
    enabled: false
    esp32:
      host: ""
      port: 8888
      use_broadcast: true
    target:
      width: 170
      height: 320
    source:
      type: window
      window:
        title: "Chrome"       # 窗口标题关键字（支持部分匹配）
        crop_alignment: "center"  # left | center | right
    diff:
      target_fps: 24.0

  # 系统性能信息示例
  - name: "SystemInfo"
    enabled: false
    esp32:
      host: ""
      port: 8888
      use_broadcast: true
    target:
      width: 170
      height: 320
    source:
      type: sysinfo
      sysinfo:
        refresh_ms: 1000      # 采集/刷新间隔（毫秒）
        widgets:              # 等高分布，按顺序渲染
          - type: "text"
            text: "STATUS"    # 固定文本 widget
          - type: "cpu"       # CPU 使用率仪表盘
          - type: "mem"       # 内存使用率仪表盘
          - type: "net"       # 网络上传/下载速度
          - type: "disk"      # 磁盘读/写速度
```

## 数据源类型

| 类型 | 说明 | 配置项 |
|---|---|---|
| `screen` | 全屏/显示器捕获 | `screen.monitor` (0=主屏), `screen.region` (可选裁剪区域) |
| `window` | 指定窗口捕获 | `window.title` (窗口标题关键字), `window.crop_alignment` (left/center/right) |
| `sysinfo` | 系统性能仪表盘 | `sysinfo.refresh_ms`, `sysinfo.widgets` (text/cpu/mem/net/disk) |

### 窗口捕获逻辑（M3）
- 通过 `EnumWindows` + `GetWindowTextW` 枚举可见窗口
- 标题模糊匹配（包含关键字即可）
- `GetWindowRect` 获取窗口坐标，`kbinani/screenshot.CaptureRect` 抓屏
- 按目标宽高比裁剪：left/center/right 对齐（与 Python 版一致）

### 系统信息绘制（M4）
- 采集：`gopsutil` 采集 CPU/内存/网络/磁盘
- 绘制：`fogleman/gg` 绘制黑底白字绿条仪表盘
- 5 类 widget：文本、CPU、内存、网络、磁盘
- 等高分布，按配置顺序渲染

## 单元测试

```powershell
go test ./...
```

当前测试覆盖：

| 包 | 测试内容 |
|---|---|
| `internal/protocol` | 12B 头打包解包、RGB565 转换、矩形切分（与 Python 版字节级一致） |
| `internal/pipeline` | 脏矩形（曼哈顿）、自适应阈值、缩放（CatmullRom/ApproxBiLinear） |
| `internal/source` | 截屏超时错误捕获、热插拔边界保护 |

## M2 健壮性插桩

- **截屏超时**：`ScreenSource.Next` 在 2s 内未返回则报 `ErrCaptureTimeout`，pipeline 走 sleep+retry
  - 应对场景：屏保/锁屏下 Win32 GDI 调用阻塞
- **多显示器热插拔**：每次 `Next` 重新查 `NumActiveDisplays`，索引越界回退到主显示器
- **连接写失败**：`writeAll` 失败时立即关闭底层 conn，避免 stale socket 持续报错
- **指数退避重连**：1s → 2s → 4s → 8s → 16s → 30s（封顶），重连成功后重置
- **panic 恢复**：所有 loop goroutine 用 `defer recover()` 包裹，panic 计数到 `metrics.PanicRecovered`
- **错误计数**：`metrics.SourceErrors` / `SendErrors` / `PanicRecovered` atomic 计数

## 已知限制（M2 当前未做）

- `source/window` 与 `source/sysinfo` 仍是 stub，会返回 "M3/M4 not implemented yet"
- 无 GUI（CLI 模式），多 pipeline 并行需要靠 YAML 配置 + 多个终端启动
- 无日志文件落盘，关闭后日志消失
- 无健康检查 / 通知，重连过程只能看 stdout

## 目录结构

```
esp32-host-go/
├── go.mod
├── main.go                 # CLI/GUI 入口：HEADLESS=1 或 --cli 切换模式
├── internal/
│   ├── config/             # YAML 加载/校验/默认值
│   ├── protocol/           # 12B 头 + RGB565 + 切分 + 心跳
│   ├── transport/          # Discovery (UDP) + ESP32Client (TCP)
│   ├── source/             # Source 接口 + screen/window/sysinfo
│   ├── pipeline/           # 帧生成/消费/差分/编码/连接管理
│   ├── metrics/            # 内部 FPS/队列/阈值/错误指标
│   └── gui/                # Fyne GUI + 系统托盘 (M5)
├── config.example.yaml
├── scripts/build.ps1
├── README.md
└── PLAN.md                 # 完整方案文档
```
