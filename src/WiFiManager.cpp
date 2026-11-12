#include "WiFiManager.h"
#include <esp_log.h>
#include "config.h"
#include "esp_wifi.h"

static const char* TAG = "WiFi";

WiFiManager::WiFiManager() :
 webServer(nullptr),
 dnsStarted(false),
 configMode(false),
 fsServerStarted(false),
 credentialCount(0),
 scanComplete(false),
 networksFound(0),
 apStarted(false),
 apStartTime(0),
 reconnectCount(0),
 initialized(false),
 _initialConnected(false),
 smartConfigStarted(false),
 smartConfigDone(false),
 smartConfigStartTime(0),
 _configModeStartTime(0),
 _configDisabled(false),
 _reconnectAttempts(0),
 _reconnectWaitUntil(0),
 _netReady(false),
 _activeCredIndex(-1) {
}

bool WiFiManager::connect() {
 ESP_LOGI(TAG, "Starting STA mode connection...");

 if (!initialized) {
  begin();
  initialized = true;
 }

 // 先扫描，仅连接“可见”的已存 WiFi
 if (autoConnect()) {
  ESP_LOGI(TAG, "STA mode connected successfully!");
  onConnected();
  return true;
 }

 // 无可见的已存 WiFi（或全部失败）→ 进入配网模式（SmartConfig → AP 门户）
 ESP_LOGI(TAG, "No visible saved WiFi, entering config mode...");
 startConfigMode();
 return false;
}

void WiFiManager::onConnected() {
 _netReady = true;          // 置位联网就绪标志，放行联网应用
 reconnectCount = 0;
 _reconnectAttempts = 0;
 _reconnectWaitUntil = 0;
 _initialConnected = true;
}

void WiFiManager::maintainConnection() {
 // 门户（自动配网 / 手动 AP）的 Web 服务需周期性处理请求
 handleClient();

 // 手动 AP 模式（用户长按触发）：仅维持门户，优先于"禁用"判断，允许超时后手动配网
 if (apStarted && !configMode) return;

 // 配网已超时禁用：彻底关闭射频，只能重启后再配网
 if (_configDisabled) {
  if (WiFi.getMode() != WIFI_OFF) {
   ESP_LOGW(TAG, "[WiFi] 配网已禁用，关闭射频（重启后可重新配网）");
   WiFi.mode(WIFI_OFF);
  }
  _netReady = false;
  return;
 }

 // 配网模式（SmartConfig → AP 门户）
 if (isConfigMode()) {
  handleConfigMode();
  return;
 }

 // 已连接：置位联网标志并复位重连计数
 if (WiFi.status() == WL_CONNECTED) {
  onConnected();
  return;
 }

 // 未连接：清除联网标志（阻止联网应用访问网络），进入退避重连
 _netReady = false;
 handleReconnect();
}

void WiFiManager::startConfigMode() {
 configMode = true;
 _configModeStartTime = millis();
 _netReady = false;

 ESP_LOGI(TAG, "[WiFi] 进入配网模式：立即开启热点门户（Captive Portal），总窗口 %lu 分钟",
               CONFIG_MODE_TIMEOUT_MS / 60000);

 // 热点优先：无凭据/连接失败时立刻开出配网热点，手机连上即可自动弹出配置页。
 // 注意：不在此自动启用 SmartConfig —— Arduino 的 beginSmartConfig() 会强制切到
 // WIFI_STA 并拆掉 SoftAP，与"热点立即可用"冲突（单射频无法并行）。
 startConfigPortal();
}

void WiFiManager::handleConfigMode() {
 unsigned long elapsed = millis() - _configModeStartTime;

 // STA 已连上（门户配置生效后重启前，或已有凭据自行连上）→ 配网成功
 if (WiFi.status() == WL_CONNECTED) {
  ESP_LOGI(TAG, "[WiFi] 配网成功! IP: %s", WiFi.localIP().toString().c_str());
  stopSmartConfig();
  stopAPMode();
  configMode = false;
  onConnected();
  return;
 }

 // 配网总窗口超时：关闭 WiFi，禁止再次自动配网（只能重启后配置）
 if (elapsed >= CONFIG_MODE_TIMEOUT_MS) {
  ESP_LOGW(TAG, "[WiFi] 配网超时（%lu 分钟），关闭 WiFi，重启后可重新配网", CONFIG_MODE_TIMEOUT_MS / 60000);
  stopSmartConfig();
  stopAPMode();
  WiFi.mode(WIFI_OFF);
  configMode = false;
  _configDisabled = true;
  _netReady = false;
 }
}

void WiFiManager::handleReconnect() {
 unsigned long now = millis();

 // 5 次重试失败后的“等待期”：期间不做任何连接尝试
 if (_reconnectWaitUntil != 0) {
  if (now < _reconnectWaitUntil) return;
  _reconnectWaitUntil = 0;
  _reconnectAttempts = 0;
 }

 if (_reconnectAttempts >= RECONNECT_RETRY_MAX) {
  ESP_LOGW(TAG, "[WiFi] 已重试 %d 次仍失败，等待 %lu 分钟后再试",
                RECONNECT_RETRY_MAX, RECONNECT_WAIT_MS / 60000);
  _reconnectWaitUntil = now + RECONNECT_WAIT_MS;
  return;
 }

 _reconnectAttempts++;
 reconnectCount++;
 ESP_LOGI(TAG, "[WiFi] 断线重连 第 %d/%d 次", _reconnectAttempts, RECONNECT_RETRY_MAX);

 // 优先重连上次成功的凭据，不重新扫描（路由器关闭时扫描无意义且耗电）
 int idx = (_activeCredIndex >= 0 && _activeCredIndex < credentialCount) ? _activeCredIndex : 0;
 if (connectSavedIndex(idx, RECONNECT_RETRY_INTERVAL_MS)) {
  ESP_LOGI(TAG, "[WiFi] 重连成功! IP: %s", WiFi.localIP().toString().c_str());
  onConnected();
 }
}

bool WiFiManager::connectSavedIndex(int idx, unsigned long timeoutMs) {
 if (idx < 0 || idx >= credentialCount) return false;

 WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
 WiFi.begin(savedCredentials[idx].ssid.c_str(), savedCredentials[idx].password.c_str());
 esp_wifi_set_max_tx_power(WIFI_TX_POWER_RAW);

 unsigned long start = millis();
 while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
  delay(500);
 }

 if (WiFi.status() == WL_CONNECTED) {
  _activeCredIndex = idx;
  ESP_LOGI(TAG, "[WiFi] SSID: %s connected", savedCredentials[idx].ssid.c_str());
  return true;
 }

 WiFi.disconnect(false);
 delay(100);
 return false;
}

bool WiFiManager::isConnected() {
 // 唯一联网开关：未连接时为 false，阻止 TimeManager/WeatherManager 等联网应用访问网络
 return _netReady;
}

int WiFiManager::getReconnectCount() {
 return reconnectCount;
}

int WiFiManager::getRSSI() {
 return WiFi.RSSI();
}

const char* WiFiManager::getLocalIP() {
 static String ipStr;
 ipStr=WiFi.localIP().toString();
 return ipStr.c_str();
}


const char* WiFiManager::getdnsIP() {
 static String ipStr;
 ipStr=WiFi.dnsIP().toString();
 return ipStr.c_str();
}

const char* WiFiManager::getSSID() {
 static String ssidStr;
 if (WiFi.status() == WL_CONNECTED) {
  ssidStr = WiFi.SSID();
 } else {
  ssidStr = "";
 }
 return ssidStr.c_str();
}

bool WiFiManager::isAPMode() {
 return isConfigMode();
}

void WiFiManager::handleClient() {
 // Captive Portal：处理 DNS 查询（把任意域名应答为 AP 地址）
 if (dnsStarted) {
  dnsServer.processNextRequest();
 }
 if (webServer) {
  webServer->handleClient();
 }
}

void WiFiManager::startAPMode() {
 if (apStarted) return;

 // 手动 AP 优先（用户长按触摸触发）：终止自动配网（SmartConfig / 配网窗口）
 stopSmartConfig();
 configMode = false;

 ESP_LOGI(TAG, "Starting AP mode...");

 WiFi.mode(WIFI_AP_STA);

 WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
 updateApSsid();
 WiFi.softAP(apSsid.c_str(), NULL, 1, false, 4);

 delay(500);

 ESP_LOGI(TAG, "AP IP: %s", WiFi.softAPIP().toString().c_str());
 ESP_LOGI(TAG, "AP SSID: %s", WiFi.softAPSSID().c_str());

 apStarted = true;
 apStartTime = millis();

 startPortalDns();
 startWebServer();
}

void WiFiManager::stopAPMode() {
 stopPortalDns();

 if (!apStarted) return;

 ESP_LOGI(TAG, "Stopping AP mode...");
 WiFi.softAPdisconnect(true);
 delay(100);

 stopWebServer();

 apStarted = false;
 apStartTime = 0;
 configMode = false;

 ESP_LOGI(TAG, "AP mode stopped");
}

void WiFiManager::stopWebServer() {
 if (webServer) {
  webServer->stop();
  delete webServer;
  webServer = nullptr;
 }
 fsServerStarted = false;
}

void WiFiManager::startPortalDns() {
 if (dnsStarted) {
  dnsServer.stop();
  dnsStarted = false;
 }

 // 把所有域名解析到热点地址，触发手机/PC 的 Captive Portal 检测
 dnsStarted = dnsServer.start(53, "*", WiFi.softAPIP());
 if (dnsStarted) {
  ESP_LOGI(TAG, "[CaptivePortal] DNS 劫持已启动 -> %s", WiFi.softAPIP().toString().c_str());
 } else {
  ESP_LOGW(TAG, "[CaptivePortal] DNS 劫持启动失败（不影响手动打开 http://192.168.4.1/）");
 }
}

void WiFiManager::stopPortalDns() {
 if (!dnsStarted) return;
 dnsServer.stop();
 dnsStarted = false;
 ESP_LOGI(TAG, "[CaptivePortal] DNS 劫持已停止");
}

void WiFiManager::updateApSsid() {
 uint8_t mac[6] = {0};
 WiFi.macAddress(mac);
 char buf[16];
 snprintf(buf, sizeof(buf), "esp32-%02x%02x", mac[4], mac[5]);
 apSsid = buf;
}

bool WiFiManager::isAPStarted() {
 return apStarted;
}

bool WiFiManager::hasInitialConnected() const {
 return _initialConnected;
}

void WiFiManager::begin() {
 ESP_LOGI(TAG, "Initializing NVS...");
 if (preferences.begin("wifi-config", false)) {
  ESP_LOGI(TAG, "NVS initialized successfully");
 } else {
  ESP_LOGI(TAG, "NVS read-only failed, trying read-write...");
  if (preferences.begin("wifi-config", true)) {
   ESP_LOGI(TAG, "NVS opened in read-write mode");
  } else {
   ESP_LOGE(TAG, "NVS open FAILED!");
  }
 }

 loadCredentials();
 ESP_LOGI(TAG, "Initialized");

 // 置位后 connect() 不再重复调用 begin()，避免 Preferences 二次 begin 返回 false
 initialized = true;
}

bool WiFiManager::autoConnect() {
 if (!hasSavedCredentials()) {
  ESP_LOGW(TAG, "[WiFi] 无已存凭据，跳过自动连接");
  return false;
 }

 ESP_LOGI(TAG, "Trying saved WiFi credentials...");

 // 先扫描环境：只连接“可见”的已存 WiFi，避免对不可见凭据盲等 20s
 WiFi.disconnect(false);
 delay(100);
 int found = WiFi.scanNetworks(false, false);

 // 扫描本身失败 → 退化为逐个尝试已存凭据（保留容错）
 if (found < 0) {
  ESP_LOGW(TAG, "[WiFi] 扫描失败(%d)，逐个尝试已存凭据", found);
  for (int i = 0; i < credentialCount; i++) {
   if (connectSavedIndex(i, WIFI_CONNECT_TIMEOUT_MS)) return true;
  }
  return false;
 }

 // 挑出可见的已存凭据，记录 RSSI
 int rssi[MAX_WIFI_CREDENTIALS];
 int order[MAX_WIFI_CREDENTIALS];
 int visible = 0;
 for (int i = 0; i < credentialCount; i++) {
  rssi[i] = -127;  // 未发现
  for (int j = 0; j < found; j++) {
   if (savedCredentials[i].ssid == WiFi.SSID(j)) {
    rssi[i] = WiFi.RSSI(j);
    break;
   }
  }
  if (rssi[i] != -127) order[visible++] = i;
 }
 WiFi.scanDelete();

 if (visible == 0) {
  ESP_LOGW(TAG, "[WiFi] 已存凭据均不可见（扫描到 %d 个网络），不尝试连接", found);
  return false;
 }

 // 可见凭据按 RSSI 降序（强信号优先）
 for (int i = 0; i < visible; i++) {
  for (int j = i + 1; j < visible; j++) {
   if (rssi[order[j]] > rssi[order[i]]) {
    int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
   }
  }
 }

 for (int k = 0; k < visible; k++) {
  const int i = order[k];
  ESP_LOGI(TAG, "[WiFi] 尝试可见 SSID: %s (RSSI %d)", savedCredentials[i].ssid.c_str(), rssi[i]);
  if (connectSavedIndex(i, WIFI_CONNECT_TIMEOUT_MS)) {
   ESP_LOGI(TAG, "\n[WiFi] Saved WiFi connected!");
   return true;
  }
  ESP_LOGI(TAG, "[WiFi] SSID %s failed, trying next...", savedCredentials[i].ssid.c_str());
 }

 ESP_LOGW(TAG, "[WiFi] 可见凭据均连接失败");
 return false;
}

void WiFiManager::startConfigPortal() {
 ESP_LOGI(TAG, "Starting Config Portal...");

 configMode = true;

 WiFi.mode(WIFI_AP_STA);
 // 用 false 保持 AP_STA：若用 true，模式会降为「仅 AP」，之后门户点「扫描WiFi」时
 // WiFiScanClass::scanNetworks() 内部 enableSTA(true) 会触发模式切换并重启驱动，
 // 踢掉正在等 /scan 响应的手机，前端 fetch 直链失败（页面显示「扫描失败」）
 WiFi.disconnect(false);
 delay(100);

 WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
 updateApSsid();
 WiFi.softAP(apSsid.c_str(), NULL, 1, false, 4);

 delay(500);

 ESP_LOGI(TAG, "AP IP: %s", WiFi.softAPIP().toString().c_str());
 ESP_LOGI(TAG, "AP SSID: %s", WiFi.softAPSSID().c_str());

 apStarted = true;
 apStartTime = millis();

 startPortalDns();
 startWebServer();
}

void WiFiManager::startWebServer() {
 if (webServer) {
  stopWebServer();
 }
 webServer = new WebServer(80);

 webServer->on("/", HTTP_GET, [this]() {
  handleRoot();
 });

 webServer->on("/scan", HTTP_GET, [this]() {
  handleScan();
 });

 webServer->on("/save", HTTP_POST, [this]() {
  handleSave();
 });

 webServer->on("/forget", HTTP_POST, [this]() {
  handleForget();
 });

 webServer->on("/fs", HTTP_GET, [this]() {
  handleFS();
 });

 webServer->on("/download", HTTP_GET, [this]() {
  handleDownload();
 });

 webServer->on("/upload", HTTP_POST, [this]() {
  handleUploadEnd();
 }, [this]() {
  handleUpload();
 });

 webServer->on("/delete", HTTP_GET, [this]() {
  handleDelete();
 });

 webServer->onNotFound([this]() {
  handleNotFound();
 });

 webServer->begin();
 fsServerStarted = true;
 ESP_LOGI(TAG, "Web server started (with FS)");
}

void WiFiManager::handleRoot() {
 String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
 <meta charset='UTF-8'>
 <meta name="viewport" content="width=device-width,initial-scale=1.0">
 <title>ESP32 Weather Clock WiFi配置</title>
 <style>
 * { margin: 0; padding: 0; box-sizing: border-box; }
 body { font-family: Arial, sans-serif; background: #f0f0f0; padding: 20px; }
 .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
 h2 { text-align: center; color: #333; margin-bottom: 20px; }
 h3 { color: #555; margin-top: 20px; margin-bottom: 10px; border-bottom: 1px solid #eee; padding-bottom: 5px; }
 .form-group { margin-bottom: 15px; }
 label { display: block; margin-bottom: 5px; color: #666; }
 select, input { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
 button { width: 100%; padding: 12px; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin-bottom: 10px; }
 button.scan { background: #2196F3; color: white; }
 button.scan:hover { background: #1976D2; }
 button.save { background: #4CAF50; color: white; }
 button.save:hover { background: #45a049; }
 button.forget { background: #f44336; color: white; font-size: 12px; padding: 6px 10px; width: auto; }
 button.forget:hover { background: #d32f2f; }
 .status { text-align: center; margin-top: 10px; color: #666; }
 .loading { text-align: center; padding: 20px; }
 .saved-list { list-style: none; padding: 0; }
 .saved-list li { background: #f8f8f8; padding: 10px; border-radius: 5px; margin-bottom: 8px; display: flex; justify-content: space-between; align-items: center; }
 .saved-list li .ssid-name { font-weight: bold; color: #333; }
 .saved-list li .password-hint { font-size: 12px; color: #999; }
 .no-saved { color: #999; text-align: center; padding: 10px; }
 </style>
</head>
<body>
 <div class="container">
 <h2>WiFi 配置</h2>
 <div class="form-group">
 <label>选择WiFi网络:</label>
 <select id="wifiSelect">
 <option value="">-- 点击扫描 --</option>
 </select>
 </div>
 <button class="scan" onclick="scanNetworks()">扫描WiFi</button>
 <div class="form-group">
 <label>WiFi密码:</label>
 <input type="text" id="password" placeholder="请输入密码">
 </div>
 <button class="save" onclick="saveConfig()">保存并连接</button>
 <div class="status" id="status"></div>

 <h3>已保存的WiFi网络</h3>
 <ul id="savedList" class="saved-list">)rawliteral";

 for (int i = 0; i < credentialCount; i++) {
  String ssid = savedCredentials[i].ssid;
  String escapedSsid = ssid;
  escapedSsid.replace("\"", "\\\"");
  escapedSsid.replace("'", "\\'");
  String maskedPass;
  for (int j = 0; j < (int)savedCredentials[i].password.length(); j++) {
   maskedPass += "*";
  }
  html += "<li><div><span class='ssid-name'>" + ssid + "</span><br><span class='password-hint'>密码: " + maskedPass + "</span></div><button class='forget' onclick=\"forgetWifi('" + escapedSsid + "')\">删除</button></li>";
 }

 if (credentialCount == 0) {
  html += "<li class='no-saved'>暂无已保存的WiFi网络</li>";
 }

 html += R"rawliteral(</ul>
 </div>
 <script>
 function scanNetworks() {
  var select = document.getElementById('wifiSelect');
  select.innerHTML = '<option value="">-- 扫描中... --</option>';

  fetch('/scan')
  .then(r => r.json())
  .then(data => {
   select.innerHTML = '';
   data.forEach(net => {
    var opt = document.createElement('option');
    opt.value = net.ssid;
    opt.text = net.ssid + ' (' + net.rssi + ' dBm)';
    select.appendChild(opt);
   });
   if (data.length === 0) {
    select.innerHTML = '<option value="">未找到WiFi</option>';
   }
  })
  .catch(err => {
   select.innerHTML = '<option value="">扫描失败</option>';
  });
 }

 function saveConfig() {
  var ssid = document.getElementById('wifiSelect').value;
  var password = document.getElementById('password').value;

  if (!ssid) {
   document.getElementById('status').textContent = '请先扫描并选择WiFi网络';
   return;
  }

  document.getElementById('status').textContent = '正在保存...';

  var formData = new FormData();
  formData.append('ssid', ssid);
  formData.append('password', password);

  fetch('/save', { method: 'POST', body: formData })
  .then(r => r.text())
  .then(data => {
   document.getElementById('status').innerHTML = '配置已保存，设备将重启<br><small>请重新连接WiFi</small>';
  })
  .catch(err => {
   document.getElementById('status').textContent = '保存失败';
  });
 }

 function forgetWifi(ssid) {
  if (!confirm('确定要删除此WiFi配置吗？')) {
   return;
  }

  fetch('/forget?ssid=' + encodeURIComponent(ssid), { method: 'POST' })
  .then(r => r.text())
  .then(data => {
   location.reload();
  })
  .catch(err => {
   alert('删除失败');
  });
 }
 </script>
</body>
</html>
)rawliteral";

 webServer->send(200, "text/html", html);
}

void WiFiManager::handleScan() {
 ESP_LOGI(TAG, "Scanning networks...");

 WiFi.disconnect(false);
 delay(100);

 networksFound = WiFi.scanNetworks(false, false);
 String json = "[";
 for (int i = 0; i < networksFound; i++) {
  if (i > 0) json += ",";
  String ssid = WiFi.SSID(i);
  ssid.replace("\"", "\\\"");
  json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
 }
 json += "]";

 webServer->send(200, "application/json", json);

 WiFi.scanDelete();
}

void WiFiManager::handleSave() {
 String ssid = webServer->arg("ssid");
 String password = webServer->arg("password");

 ESP_LOGI(TAG, "Saving credentials...");
 ESP_LOGI(TAG, "Saved WiFi credentials, SSID: %s", ssid.c_str());

 saveCredentials(ssid, password);

 webServer->send(200, "text/plain", "OK");

 delay(1000);
 ESP.restart();
}

void WiFiManager::handleForget() {
 String ssid = webServer->arg("ssid");

 ESP_LOGI(TAG, "Forgetting credentials...");
 ESP_LOGI(TAG, "Forgot WiFi credentials, SSID: %s", ssid.c_str());

 forgetCredentials(ssid);

 webServer->send(200, "text/plain", "OK");
}

void WiFiManager::handleNotFound() {
 // Captive Portal：DNS 已把任意域名解析到本机，这里把所有未注册路径
 // （/generate_204、/hotspot-detect.html、/connecttest.txt、/canonical.html 等
 // 各系统的联网探测地址）统一 302 回配置页，系统据此判定"需要登录"并自动弹窗。
 String url = "http://" + WiFi.softAPIP().toString() + "/";
 webServer->sendHeader("Location", url, true);
 webServer->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
 webServer->send(302, "text/html", "<html><body><a href='" + url + "'>WiFi 配置</a></body></html>");
}

void WiFiManager::connectToWiFi(const char* ssid, const char* password, int timeoutMs) {
 WiFi.mode(WIFI_STA);
 WiFi.begin(ssid, password);

 unsigned long startTime = millis();
 while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
  delay(500);
  ESP_LOGI(TAG, ".");
 }
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
 for (int i = 0; i < credentialCount; i++) {
  if (savedCredentials[i].ssid == ssid) {
   for (int j = i; j > 0; j--) {
    savedCredentials[j] = savedCredentials[j - 1];
   }
   savedCredentials[0].ssid = ssid;
   savedCredentials[0].password = password;
   goto save;
  }
 }

 if (credentialCount < MAX_WIFI_CREDENTIALS) {
  for (int i = credentialCount; i > 0; i--) {
   savedCredentials[i] = savedCredentials[i - 1];
  }
  savedCredentials[0].ssid = ssid;
  savedCredentials[0].password = password;
  credentialCount++;
 } else {
  for (int i = MAX_WIFI_CREDENTIALS - 1; i > 0; i--) {
   savedCredentials[i] = savedCredentials[i - 1];
  }
  savedCredentials[0].ssid = ssid;
  savedCredentials[0].password = password;
 }

save:
 preferences.putInt("count", credentialCount);
 for (int i = 0; i < credentialCount; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.putString(ssidKey.c_str(), savedCredentials[i].ssid);
  preferences.putString(passKey.c_str(), savedCredentials[i].password);
 }
 ESP_LOGI(TAG, "Saved %d credentials to NVS", credentialCount);
}

bool WiFiManager::loadCredentials() {
 credentialCount = preferences.getInt("count", 0);

 if (credentialCount == 0) {
  String oldSSID = preferences.getString("ssid", "");
  String oldPass = preferences.getString("password", "");
  if (oldSSID.length() > 0) {
   savedCredentials[0].ssid = oldSSID;
   savedCredentials[0].password = oldPass;
   credentialCount = 1;
   ESP_LOGI(TAG, "[WiFi] Migrated old SSID: %s", oldSSID.c_str());
  }
  return credentialCount > 0;
 }

 for (int i = 0; i < credentialCount && i < MAX_WIFI_CREDENTIALS; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  savedCredentials[i].ssid = preferences.getString(ssidKey.c_str(), "");
  savedCredentials[i].password = preferences.getString(passKey.c_str(), "");
 }

 ESP_LOGI(TAG, "Loaded %d credentials", credentialCount);
 return credentialCount > 0;
}

bool WiFiManager::hasSavedCredentials() {
 return credentialCount > 0;
}

IPAddress WiFiManager::getIP() {
 if (configMode) {
  return WiFi.softAPIP();
 }
 return WiFi.localIP();
}

bool WiFiManager::isConfigMode() {
 return configMode;
}

const char* WiFiManager::getConfigSSID() {
 return apSsid.c_str();
}

const char* WiFiManager::getConfigPassword() {
 return "";
}

unsigned long WiFiManager::getAPStartTime() {
 return apStartTime;
}

const WiFiCredential* WiFiManager::getSavedCredentials() const {
 return savedCredentials;
}

int WiFiManager::getCredentialCount() const {
 return credentialCount;
}

void WiFiManager::forgetCredentials(const String& ssid) {
 for (int i = 0; i < credentialCount; i++) {
  if (savedCredentials[i].ssid == ssid) {
   for (int j = i; j < credentialCount - 1; j++) {
    savedCredentials[j] = savedCredentials[j + 1];
   }
   credentialCount--;
   break;
  }
 }

 preferences.putInt("count", credentialCount);
 for (int i = 0; i < credentialCount; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.putString(ssidKey.c_str(), savedCredentials[i].ssid);
  preferences.putString(passKey.c_str(), savedCredentials[i].password);
 }

 for (int i = credentialCount; i < MAX_WIFI_CREDENTIALS; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.remove(ssidKey.c_str());
  preferences.remove(passKey.c_str());
 }

 ESP_LOGI(TAG, "Forgetting SSID, %d credentials remaining", credentialCount);
}

void WiFiManager::scanNetworks() {
 networksFound = WiFi.scanNetworks(false, false);
 scanComplete = true;
}

void WiFiManager::startSmartConfig() {
 if (smartConfigStarted) return;

 ESP_LOGI(TAG, "[SmartConfig] Starting ESP-Touch SmartConfig...");

 WiFi.beginSmartConfig();
 smartConfigStarted = true;
 smartConfigDone = false;
 smartConfigStartTime = millis();

 ESP_LOGI(TAG, "[SmartConfig] Waiting for ESP-Touch packet...");
}

void WiFiManager::stopSmartConfig() {
 if (!smartConfigStarted) return;

 ESP_LOGI(TAG, "[SmartConfig] Stopping SmartConfig...");
 WiFi.stopSmartConfig();
 smartConfigStarted = false;
 smartConfigDone = false;
 smartConfigStartTime = 0;

 ESP_LOGI(TAG, "[SmartConfig] Stopped");
}

bool WiFiManager::isSmartConfigStarted() {
 return smartConfigStarted;
}

bool WiFiManager::isSmartConfigDone() {
 return smartConfigDone;
}

void WiFiManager::handleSmartConfig() {
	if (!smartConfigStarted) return;

	if (WiFi.smartConfigDone()) {
		ESP_LOGI(TAG, "[SmartConfig] SmartConfig received!");
		smartConfigDone = true;

		String ssid, password;
		int retry = 0;
		while (retry < 10) {
			ssid = WiFi.SSID();
			password = WiFi.psk();
			if (ssid.length() > 0 && password.length() > 0) break;
			delay(50);
			retry++;
		}

		ESP_LOGI(TAG, "[SmartConfig] Received SSID: %s, password length: %d", ssid.c_str(), password.length());

		if (ssid.length() > 0) {
			ESP_LOGI(TAG, "[SmartConfig] Saving credentials to NVS...");
			saveCredentials(ssid, password);
			_activeCredIndex = 0;   // 新保存的凭据位于索引 0
			ESP_LOGI(TAG, "[SmartConfig] Credentials saved successfully, SSID: %s", ssid.c_str());
		} else {
			ESP_LOGW(TAG, "[SmartConfig] Failed to get valid SSID after retries, not saving");
		}
	}

	if (smartConfigStartTime > 0 && (millis() - smartConfigStartTime) >= SMART_CONFIG_TIMEOUT_MS) {
		ESP_LOGI(TAG, "[SmartConfig] Timeout (%lu s), stopping SmartConfig...", SMART_CONFIG_TIMEOUT_MS / 1000);
		stopSmartConfig();
	}
}

String WiFiManager::sizeStr(uint64_t bytes) {
 if (bytes < 1024) return String(bytes) + " B";
 if (bytes < 1024 * 1024) return String(bytes / 1024) + " KB";
 return String(bytes / (1024 * 1024)) + " MB";
}

void WiFiManager::handleFS() {
 String path = webServer->arg("path");
 if (path.length() == 0) path = "/";

 String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
 "<style>body{font-family:Arial,sans-serif;margin:10px;background:#222;color:#eee}a{color:#4af;text-decoration:none}"
 "h2{color:#fff;border-bottom:1px solid #555}pre{background:#111;padding:8px;border-radius:4px}"
 "input,button{padding:6px;margin:4px 0}button{background:#07c;color:#fff;border:none;border-radius:4px;cursor:pointer}"
 ".saved{color:#0a0}.del{color:#f44;font-size:12px}</style></head><body>";
 html += "<h2>LittleFS: " + path + "</h2>";

 if (path != "/") {
  String parent = path;
  if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
  int pos = parent.lastIndexOf('/');
  if (pos <= 0) parent = "/";
  else parent = parent.substring(0, pos);
  html += "<a href='/fs?path=" + parent + "'>[返回上级]</a><br>";
 }

 fs::File dir = LittleFS.open(path);
 if (!dir || !dir.isDirectory()) {
  html += "<p>不是目录</p>";
  html += "<hr><a href='/'>Home</a></body></html>";
  webServer->send(200, "text/html", html);
  return;
 }

 fs::File f;
 while ((f = dir.openNextFile())) {
  String name = String(f.name());
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  if (name == ".") { f.close(); continue; }

  String fullPath = path;
  if (fullPath != "/") fullPath += "/";
  fullPath += name;

  if (f.isDirectory()) {
   html += "<a href='/fs?path=" + fullPath + "'>[目录] " + name + "/</a><br>";
  } else {
   html += "<a href='/download?path=" + fullPath + "'>" + name + "</a> ";
   html += "<span style='color:#888'>" + sizeStr(f.size()) + "</span> ";
   html += "<a class='del' href='/delete?path=" + fullPath + "' onclick=\"return confirm('确定删除 " + name + "?')\">[删除]</a>";
   html += "<br>";
  }
  f.close();
 }
 dir.close();

 html += "<hr><form action='/upload' method='post' enctype='multipart/form-data'>";
 html += "<input type='hidden' name='path' value='" + path + "'>";
 html += "<input type='file' name='file'><button type='submit'>上传文件</button></form>";

 html += "<hr><a href='/'>Home</a></body></html>";
 webServer->send(200, "text/html", html);
}

void WiFiManager::handleDownload() {
 String path = webServer->arg("path");
 if (path.length() == 0) { webServer->send(404, "text/plain", "路径为空"); return; }

 fs::File f = LittleFS.open(path);
 if (!f || f.isDirectory()) { webServer->send(404, "text/plain", "文件不存在"); return; }

 String name = path;
 int slash = name.lastIndexOf('/');
 if (slash >= 0) name = name.substring(slash + 1);

 webServer->setContentLength(f.size());
 webServer->send(200, "application/octet-stream", "");
 webServer->streamFile(f, "application/octet-stream");
 f.close();
}

void WiFiManager::handleUpload() {
 HTTPUpload& up = webServer->upload();
 String path = webServer->arg("path");
 if (path.length() == 0) path = "/";

 if (up.status == UPLOAD_FILE_START) {
  String filename = path;
  if (filename != "/") filename += "/";
  filename += up.filename;
  uploadFile = LittleFS.open(filename, "w");
 } else if (up.status == UPLOAD_FILE_WRITE) {
  if (uploadFile) uploadFile.write(up.buf, up.currentSize);
 } else if (up.status == UPLOAD_FILE_END) {
  if (uploadFile) uploadFile.close();
 }
}

void WiFiManager::handleUploadEnd() {
 webServer->sendHeader("Location", "/fs?path=" + webServer->arg("path"));
 webServer->send(303);
}

void WiFiManager::handleDelete() {
 String path = webServer->arg("path");
 if (path.length() == 0) { webServer->send(400, "text/plain", "路径为空"); return; }

 fs::File f = LittleFS.open(path);
 if (!f) { webServer->send(404, "text/plain", "文件不存在"); return; }
 bool isDir = f.isDirectory();
 f.close();

 if (isDir) LittleFS.rmdir(path);
 else LittleFS.remove(path);

 String parent = path;
 if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
 int pos = parent.lastIndexOf('/');
 if (pos <= 0) parent = "/";
 else parent = parent.substring(0, pos);

 webServer->sendHeader("Location", "/fs?path=" + parent);
 webServer->send(303);
}
