# ESP32-C3 天气时钟 - WiFi双模式实现计划

## 项目概述

为ESP32-C3天气时钟实现WiFi双模式功能：
1. 优先尝试连接config.h中配置的默认WiFi
2. 连接失败时启动AP模式，允许手动配置WiFi
3. 自动扫描可用WiFi网络供用户选择
4. 保存用户配置的WiFi到NVS，下次优先连接

## 现有代码分析

### 当前状态
- `WiFiManager.cpp/h`: 简单的WiFi连接管理，使用硬编码凭证
- `config.h`: 存储WiFi SSID和密码
- `Wifi.html`: 现有的配置页面表单（无WiFi扫描功能）
- `main.cpp`: 主程序，使用WiFiManager连接

### 问题
- WiFi凭证硬编码，无法运行时修改
- 连接失败无备用方案
- 缺少Web服务器实现

## 连接优先级

1. **最高优先**: NVS中保存的用户配置WiFi
2. **次优先**: config.h中的默认WiFi
3. **最后**: AP配置模式

## 实施计划

### 步骤1: 创建 WiFiConfigManager.h
**文件**: `include/WiFiConfigManager.h`

声明以下方法：
- `begin()` - 初始化，从NVS加载保存的凭证
- `autoConnect()` - 按优先级尝试连接WiFi（优先NVS → 默认config）
- `startConfigPortal()` - 启动AP模式和Web服务器
- `scanNetworks()` - 扫描可用WiFi
- `saveCredentials()` - 保存WiFi凭证到NVS
- `loadCredentials()` - 从NVS加载凭证
- `getIP()` - 获取当前IP
- `isConfigMode()` - 是否处于配置模式
- `hasSavedCredentials()` - 是否有保存的凭证

### 步骤2: 创建 WiFiConfigManager.cpp
**文件**: `src/WiFiConfigManager.cpp`

实现功能：
1. **NVS存储**: 使用Preferences库存储WiFi SSID和密码
2. **自动连接（按优先级）**:
   - 首先尝试连接NVS保存的WiFi（如果有）
   - 如果NVS无保存或连接失败，尝试连接config.h中的默认WiFi
   - 超时后启动AP配置模式
3. **AP模式**:
   - 创建WiFi热点 (ESP32-WeatherClock)
   - 启动Web服务器 (端口80)
   - 处理 `/scan` 返回JSON格式的WiFi列表
   - 处理 `/save` 保存用户选择的WiFi凭证到NVS
   - 重启并尝试连接保存的凭证

4. **WiFi扫描**: 返回JSON数组包含SSID和RSSI

### 步骤3: 更新 Wifi.html
**文件**: `src/Wifi.html`

功能：
1. 页面加载时自动调用 `/scan` 获取WiFi列表
2. 下拉菜单选择WiFi网络
3. 输入密码
4. 显示城市代码输入框（可选）
5. 提交到 `/save`

### 步骤4: 更新 WiFiManager.h
**文件**: `include/WiFiManager.h`

添加：
- 构造函数接受WiFiConfigManager引用
- 新增`isAPMode()`方法
- 新增`getConfigSSID()`方法

### 步骤5: 更新 WiFiManager.cpp
**文件**: `src/WiFiManager.cpp`

修改：
- 构造函数初始化WiFiConfigManager
- `connect()`方法使用WiFiConfigManager的autoConnect
- 添加AP模式支持

### 步骤6: 更新 main.cpp
**文件**: `src/main.cpp`

修改setup()流程：
1. 初始化WiFiConfigManager
2. 尝试自动连接
3. 连接失败时显示"请配置WiFi"并启动AP
4. 连接成功后继续正常流程

添加显示状态函数：
- `showConfigMode()` - 显示配置模式提示

## 文件修改清单

| 文件路径 | 操作 | 说明 |
|---------|------|------|
| `include/WiFiConfigManager.h` | 创建 | WiFi配置管理器头文件 |
| `src/WiFiConfigManager.cpp` | 创建 | WiFi配置管理器实现 |
| `include/WiFiManager.h` | 修改 | 添加WiFiConfigManager集成 |
| `src/WiFiManager.cpp` | 修改 | 使用WiFiConfigManager |
| `src/Wifi.html` | 修改 | 添加WiFi扫描和选择功能 |
| `src/main.cpp` | 修改 | 集成WiFi双模式逻辑 |

## 依赖项

- `WiFi.h` - ESP32内置
- `WebServer.h` - ESP32内置
- `Preferences.h` - ESP32内置
- `Update.h` - ESP32内置（可选，用于OTA）

## 配置参数

```cpp
// AP模式配置
#define AP_SSID "ESP32-WeatherClock"
#define AP_PASSWORD "12345678"
#define AP_IP 192.168.4.1

// 连接超时（毫秒）
#define WIFI_CONNECT_TIMEOUT 10000
```

## 工作流程

### 正常模式
```
上电 → 检查NVS凭证 → 有 → 尝试连接NVS的WiFi
                     ↓ 无              ↓
                     尝试默认WiFi      成功 → 显示天气时钟
                           ↓
                        失败 → 启动AP配置模式
```

### 配置模式
```
上电 → 连接失败 → 启动AP → Web服务器
                            ↓
用户手机连接 → 打开配置页面 → 选择WiFi → 输入密码 → 保存到NVS
                            ↓
                       重启设备 → 优先连接保存的WiFi
```

## 预期结果

实现后：
1. 优先使用config.h中的默认WiFi（Froad-Guest）
2. 默认WiFi连接失败时，启动AP模式供用户配置其他WiFi
3. 用户配置的WiFi保存到NVS，下次上电优先使用
4. 支持手机浏览器配置WiFi
5. 自动扫描并显示可用WiFi网络供选择

## 编译验证

```bash
"C:\Users\user\.platformio\penv\Scripts\platformio.exe" run
```

## 风险评估

| 风险 | 描述 | 应对措施 |
|------|------|----------|
| Web服务器内存占用 | ESP32-C3资源有限 | 简化HTML，减少内存使用 |
| WiFi扫描阻塞 | 扫描过程可能耗时 | 使用异步扫描或设置超时 |
| NVS读写失败 | 存储异常 | 添加默认值回退 |