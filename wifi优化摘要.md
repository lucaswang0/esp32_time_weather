## 改动摘要
1. NVS 有 WiFi 时先扫描、只连可见的 — autoConnect()

- 扫描后只对「可见」的已存凭据按 RSSI 降序连接，不可见的直接跳过（不再逐个盲等 20s）。
- 扫描失败时退化为逐个尝试（保留容错）。无可见凭据 → 返回 false，触发配网。
2. 配网模式（SmartConfig → AP 门户，10 分钟超时禁用） — startConfigMode() / handleConfigMode()

- 先以 STA 模式启动 SmartConfig，30s 内未收到密码则停止并切换 AP 门户（避免`beginSmartConfig` 拆掉 AP 的冲突）。
- 配网总窗口 10 分钟；超时后`WiFi.mode(WIFI_OFF)` 、置`_configDisabled` ，自动配网禁用至重启；长按触摸手动 AP 仍可用（手动 AP 判断优先于禁用判断）。
3. 断线退避重连（路由器关闭） — handleReconnect()

- 每 10s 重连一次共 5 次，5 次失败后等待 5 分钟再循环；优先重连上次成功凭据，不重新扫描（省电/降温）。
4. 联网就绪标志 — isConnected()

- 内部`_netReady` 权威标志，未连接时置 false；`isConnected()` 返回该标志，现有联网应用（TimeManager/WeatherManager/TaskManager）自动受保护。
5. 集成调整

- taskWiFi() ：统一调用`maintainConnection()` ，修复了原「断网即重开热点」的死代码路径；新增`_lastWifiConnected` 检测新连接以刷新天气。
- setup() ：`connect()` 已自动进入配网模式，移除额外的手动`startAPMode()` 。
受影响文件 ：WiFiManager.h / WiFiManager.cpp / TaskManager.h / TaskManager.cpp / main.cpp 风险等级 ：🟡中 — 核心连接逻辑变更，且 SmartConfig→AP 的分阶段方案需实机验证射频切换是否正常（若 SmartConfig 阶段异常，可回退为仅 AP 门户）。

用户规则中「不加没要求的功能」已遵守：未新增独立 API，`isConnected()` 复用为唯一开关