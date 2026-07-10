# ESP32-C3 天气时钟项目实现计划

## 项目概述
基于ESP32-C3的天气时钟项目，显示分辨率320x170，使用TFT_eSPI库驱动屏幕。

## 核心要求
1. ✅ 保留`main.cpp`中的显示方式，不修改原有显示逻辑
2. ✅ WiFi断开后自动重连机制
3. ✅ 功能模块分不同文件存放
4. ✅ 界面显示：城市、天气、温度、年月日周时间、WiFi SSID及信号强度
5. ⏳ 后续计划：天气使用图标显示

## 现有代码分析

### 当前状态
- `main.cpp`: 使用TFT_eSPI库，包含屏幕测试和中文显示功能
- `main1.cpp.txt`: 包含WiFi连接、NTP同步、心知天气API逻辑（使用Adafruit库）
- `include/`: 已定义4个管理器头文件和配置文件

### 关键保留项
- `main.cpp`中的显示逻辑保持不变
- 使用`TFT_eSPI`库和`simhei20`中文字体

## 实施计划

### 步骤1: 创建 WiFiManager.cpp
- 从`main1.cpp`提取WiFi连接和重连逻辑
- 实现`connect()`、`maintainConnection()`、`isConnected()`、`getRSSI()`方法
- 使用`config.h`中的WiFi配置

### 步骤2: 创建 TimeManager.cpp
- 从`main1.cpp`提取NTP时间同步逻辑
- 实现`sync()`、`update()`方法和时间获取方法
- 使用`config.h`中的NTP配置

### 步骤3: 创建 WeatherManager.cpp
- 从`main1.cpp`提取天气API获取逻辑
- 实现`fetch()`方法和天气数据获取方法
- 使用`config.h`中的心知天气API配置
- 获取天气代码，为后续图标显示预留接口

### 步骤4: 更新 main.cpp
- 整合WiFiManager、TimeManager、WeatherManager
- 实现主循环逻辑
- 在原有显示框架上添加天气和WiFi信息显示
- 保留原有的中文显示和测试逻辑

## 文件修改清单

| 文件路径 | 操作 | 说明 |
|---------|------|------|
| `src/WiFiManager.cpp` | 创建 | WiFi连接管理（含重连机制） |
| `src/TimeManager.cpp` | 创建 | NTP时间同步 |
| `src/WeatherManager.cpp` | 创建 | 天气API获取（含天气代码） |
| `src/main.cpp` | 修改 | 整合所有功能，添加天气/WiFi显示 |

## 界面显示布局（保留原有风格）
- 左侧区域：日期、时间（时分秒）
- 右侧区域：城市、天气、温度、更新时间、WiFi状态（SSID和信号强度）

## 依赖与配置

### 已配置的库
- `TFT_eSPI`: 屏幕驱动
- `WiFi`: WiFi功能（ESP32内置）
- `HTTPClient`: HTTP请求
- `ArduinoJson`: JSON解析

### 配置参数
- WiFi: SSID="Froad-Guest", PASS="Tr#d5@gL"
- 心知天气: KEY="S9DGfN3MrFPxPXaG7", CITY="Shanghai"
- NTP: GMT+8 (28800秒)
- 更新间隔: 天气1800秒, 时间3600秒, WiFi检查10秒

## 命令

### 编译上传
```bash
"C:\Users\user\.platformio\penv\Scripts\platformio.exe" run --target upload
```

### 调试监控
```bash
"C:\Users\user\.platformio\penv\Scripts\platformio.exe" device monitor
```

## WiFi重连机制
1. 每10秒检查一次WiFi连接状态
2. 断开时自动尝试重连
3. 重连成功后自动同步时间和获取天气
4. 在界面上显示连接状态

## 后续计划
- **天气图标显示**: 根据天气代码（晴、多云、雨、雪等）显示对应的图标
- 图标资源准备：创建天气图标点阵数据或使用自定义字体
- 在DisplayManager中添加图标绘制方法

## 预期结果

实现后，天气时钟将：
1. 启动时连接WiFi并显示连接状态
2. 同步NTP时间，显示年月日周时分秒
3. 获取心知天气数据，显示城市、天气、温度
4. 显示WiFi SSID和信号强度
5. WiFi断开后自动重连并更新显示
6. 定期更新天气和时间数据
7. 预留天气图标显示接口，便于后续扩展