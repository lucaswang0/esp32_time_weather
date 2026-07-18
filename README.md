# ESP32-C3 智能天气时钟

基于 ESP32-C3 的智能天气时钟项目，集成环境监测、屏幕显示、WiFi通信和屏幕流传输等功能。

## 硬件平台

- **主控**: ESP32-C3 (合宙 CORE ESP32C3)
- **显示屏**: ST7789 320×170 TFT 屏幕 (SPI 接口)
- **传感器**: AHT20+BMP280 (温湿度+气压 I2C传感器)
- **触摸**: TTP223 电容触摸传感器
- **LED**: 状态指示灯（WiFi未连接时快速闪烁，正常运行时关闭）
- **蜂鸣器**: 有源蜂鸣器（低电平触发，初始化后保持高电平静音状态）

## 项目结构

```
time_weather_v2/
├── src/                    # 主程序源码
│   ├── main.cpp            # 主入口，任务调度
│   ├── PageManager.cpp     # 页面管理器
│   ├── DisplayManager.cpp  # 显示管理器（背景、字体）
│   ├── WiFiManager.cpp     # WiFi 连接管理（含 Web 配网 + ESP-Touch SmartConfig + SPIFFS文件管理）
│   ├── WeatherManager.cpp  # 和风天气 API 调用
│   ├── TimeManager.cpp     # NTP 时间同步
│   ├── AHT20BMP280Sensor.cpp # AHT20+BMP280 传感器驱动
│   ├── TTP223Sensor.cpp    # 触摸传感器驱动
│   ├── LEDController.cpp   # LED 控制
│   ├── *Page.cpp           # 各页面实现
│   └── secrets.cpp         # 敏感信息（JWT私钥等，需自行创建）
├── include/                # 头文件
│   ├── config.h            # 引脚、WiFi、更新间隔配置（需自行创建）
│   ├── secrets.h           # 敏感信息声明（extern）
│   ├── PageManager.h       # 页面枚举定义
│   ├── PageBase.h          # 页面基类
│   ├── bg1.h~bg9.h         # 背景图（RGB565 数组）
│   └── font_*.h            # 自定义字体
├── Python-ESP32-TFT-Stream/ # PC端屏幕流服务器
│   ├── server.py           # 主服务器
│   ├── pipeline.py         # 帧处理管道
│   ├── window_capture.py   # 窗口捕获
│   └── config.yaml         # 配置文件
├── platformio.ini          # PlatformIO 构建配置
└── partitions.csv          # Flash 分区表
```

## 页面功能

| 页面 | 名称 | 功能 |
|------|------|------|
| PAGE_TEMP | 温度页面 | 显示时间、日期、温度、湿度、天气图标 |
| PAGE_FORECAST | 天气预报 | 3天天气预报（日出日落、温度范围） |
| PAGE_CALENDAR | 日历 | 月历视图 |
| PAGE_PRESSURE | 气压页面 | 气压显示、气压预警（骤降触发） |
| PAGE_HISTORY | 历史页面 | 温湿度、气压曲线图（每10分钟采样） |
| PAGE_WIFI_INFO | WiFi信息 | 连接状态、SSID、IP、信号强度 |
| PAGE_AP_MODE | AP配网 | 启动时WiFi连接失败自动进入，或长按10秒进入，提供WiFi配置门户（显示已保存WiFi列表），同时支持 ESP-Touch SmartConfig 蓝牙配网 |
| PAGE_STREAMING | 屏幕流 | 接收PC端屏幕流实时显示 |

## 交互方式

| 操作 | 效果 |
|------|------|
| **短按触摸** | 切换到下一个页面 |
| **长按10秒以上** | 进入AP配网模式 |
| **双击触摸** | 循环切换背光亮度（4档） |

## 核心特性

- **智能亮度**: 根据日出日落时间自动调整背光
- **历史数据**: 每10分钟保存一次传感器数据到 SPIFFS
- **气压预警**: 气压骤降时自动切换到气压页面并警告
- **IP自动定位**: 通过公网IP自动获取所在城市经纬度，无需手动配置LOCATION
- **双模式配网**: 启动时WiFi连接失败自动进入配网模式，同时支持两种配网方式：
  - **Web 配网**: 连接 `ESP32-Weather` WiFi热点，通过浏览器访问 `192.168.4.1` 配置WiFi
  - **ESP-Touch SmartConfig**: 使用手机APP（如ESP-Touch、乐鑫官方配网工具）发送WiFi信息，无需手动连接热点
- **屏幕流传输**: 通过TCP接收PC屏幕画面
- **文件管理**: 联网后自动启动WebServer，通过浏览器访问 `http://<ESP32_IP>/fs` 进行SPIFFS文件上传/下载/删除

## 快速开始

### 1. 复制配置文件

```bash
cp include/config.h.sample include/config.h
cp src/secrets.cpp.sample src/secrets.cpp
```

### 2. 配置 WiFi

编辑 `include/config.h`:
```cpp
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
```

### 3. 配置和风天气 API

编辑 `src/secrets.cpp`:
```cpp
const char* QWEATHER_HOST = "https://your-re.qweatherapi.com";
const char* LOCATION = "101020600";
const char* JWT_KID = "YOUR_JWT_KID";
const char* JWT_SUB = "YOUR_JWT_SUB";
const char* PRIVATE_KEY = "YOUR_ED25519_PRIVATE_KEY_BASE64";
```

### 4. 编译与上传

```bash
pio run                    # 编译
pio run --target upload    # 编译并上传
pio device monitor         # 打开串口监视器
```

## Python 屏幕流服务器

### 安装依赖

```bash
cd Python-ESP32-TFT-Stream
pip install -r requirements.txt
```

### 启动服务器

```bash
python server.py
```

### 配置

编辑 `Python-ESP32-TFT-Stream/config.yaml`:
- `esp32_port`: ESP32连接端口 (默认8888)
- `target_width/height`: 目标分辨率 (320×170)
- `image_source_mode`: `WINDOW_CAPTURE` 或 `SCREEN_CAPTURE`
- `window_title`: 要捕获的窗口标题

## 使用流程

1. **编译上传**: 连接ESP32，执行 `pio run --target upload`
2. **首次启动**: ESP32自动尝试连接已保存的WiFi，如果连接失败则自动进入配网模式（AP热点 + SmartConfig），连接成功后同步NTP时间，拉取天气数据
3. **配网方式**: 
   - **Web 配网**: 在手机/电脑上连接 `ESP32-Weather` WiFi热点，浏览器访问 `192.168.4.1` 配置WiFi
   - **ESP-Touch**: 打开ESP-Touch手机APP，输入WiFi信息即可，无需连接热点
4. **正常使用**: 短按触摸切换页面，双击调整亮度，长按10秒进入配网模式
5. **屏幕流**: 在PC上启动服务器，在ESP32上切换到"屏幕流"页面接收画面
6. **文件管理**: 联网后WebServer自动启动，直接在浏览器访问 `http://<ESP32_IP>/fs`

## 注意事项

- SPIFFS分区大小为0.5MB，注意不要上传太大的文件
- WebServer在联网后自动启动，无需切换页面
- 配网模式（AP热点 + SmartConfig）在以下情况自动进入：首次启动无WiFi配置、WiFi连接失败、长按触摸键10秒以上手动触发，进入后显示10分钟倒计时
- SmartConfig 默认超时时间为2分钟，超时后自动停止，可继续使用 Web 配网
- 历史数据文件（`history_*.dat`）已加入.gitignore，不会被提交

## 依赖库

- TFT_eSPI - TFT屏幕驱动
- PNGdec - PNG解码
- ArduinoJson - JSON解析
- ArduinoUZlib - gzip解压
- DallasTemperature / OneWire - 温度传感器
- DHT sensor library - DHT传感器

## 更新日志

### 2026-07-18

- **修复**: AP配网页面倒计时10分钟后未自动退出问题
  - 修复 `startConfigPortal()` 未初始化 `apStarted` 和 `apStartTime` 的问题
  - 修复 `stopAPMode()` 未重置 `configMode` 的问题
  - 修复 `APModePage` 使用独立时间戳导致倒计时显示不准确的问题
  - 添加AP超时后自动切换回正常页面的逻辑

- **优化**: LED控制逻辑
  - 正常运行时关闭LED（无论天气数据是否获取完成）
  - WiFi未连接时快速闪烁提示用户

- **修复**: 蜂鸣器初始化逻辑
  - 蜂鸣器为低电平触发，初始化后保持高电平静音状态
  - 修复所有蜂鸣器声音停止时的电平设置

- **修复**: `BuzzerController.h` 中 `_pin` 成员变量重复声明的编译错误