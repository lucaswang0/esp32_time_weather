#include "WiFiManager.h"
#include "config.h"
#include "esp_wifi.h"

WiFiManager::WiFiManager() : 
    webServer(nullptr), 
    configMode(false),
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
    smartConfigStartTime(0) {
}

bool WiFiManager::connect() {
    Serial.println("[WiFi] Starting STA mode connection...");
    
    if (!initialized) {
        begin();
        initialized = true;
    }
    
    if (autoConnect()) {
        Serial.println("[WiFi] STA mode connected successfully!");
        reconnectCount = 0;
        _initialConnected = true;
        return true;
    }
    
    Serial.println("[WiFi] STA mode connection failed, starting config portal...");
    startConfigPortal();
    startSmartConfig();
    return false;
}

void WiFiManager::maintainConnection() {
    if (isConfigMode()) {
        handleClient();
        handleSmartConfig();
        checkAPTimeout();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WiFi] Connected via SmartConfig or Web Portal!");
            stopSmartConfig();
            stopAPMode();
            configMode = false;
            _initialConnected = true;
        }
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        reconnectCount++;
        Serial.println("\n[WiFi] Disconnected! Reconnecting...");
        
        WiFi.disconnect();
        delay(100);
        
        if (autoConnect()) {
            reconnectCount = 0;
        } else {
            Serial.println("[WiFi] Reconnect failed, will retry later");
            startConfigPortal();
            startSmartConfig();
        }
    }
    
    handleClient();
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
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

const char* WiFiManager::getSSID() {
    static String ssidStr;
    if (WiFi.status() == WL_CONNECTED) {
        ssidStr = WiFi.SSID();
    } else {
        ssidStr = WIFI_SSID;
    }
    return ssidStr.c_str();
}

bool WiFiManager::isAPMode() {
    return isConfigMode();
}

void WiFiManager::handleClient() {
    if (webServer) {
        webServer->handleClient();
    }
}

void WiFiManager::startAPMode() {
    if (apStarted) return;
    
    Serial.println("[WiFi] Starting AP mode...");
    
    WiFi.mode(WIFI_AP_STA);
    
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32-Weather", NULL, 1, false, 4);
    
    delay(500);
    
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("[WiFi] AP SSID: ");
    Serial.println(WiFi.softAPSSID());
    
    apStarted = true;
    apStartTime = millis();
    
    startWebServer();
}

void WiFiManager::stopAPMode() {
    if (!apStarted) return;
    
    Serial.println("[WiFi] Stopping AP mode...");
    
    WiFi.softAPdisconnect(true);
    delay(100);
    
    if (webServer) {
        delete webServer;
        webServer = nullptr;
    }
    
    apStarted = false;
    apStartTime = 0;
    
    Serial.println("[WiFi] AP mode stopped");
}

bool WiFiManager::isAPStarted() {
    return apStarted;
}

void WiFiManager::checkAPTimeout() {
    if (!apStarted) return;
    
    if (apStartTime > 0 && (millis() - apStartTime) >= AP_TIMEOUT_MS) {
        Serial.println("[WiFi] AP mode timeout (10 minutes), stopping AP...");
        stopAPMode();
    }
}

bool WiFiManager::hasInitialConnected() const {
    return _initialConnected;
}

void WiFiManager::begin() {
    Serial.println("[WiFi] Initializing NVS...");
    
    if (preferences.begin("wifi-config", false)) {
        Serial.println("[WiFi] NVS initialized successfully");
    } else {
        Serial.println("[WiFi] NVS read-only failed, trying read-write...");
        
        if (preferences.begin("wifi-config", true)) {
            Serial.println("[WiFi] NVS opened in read-write mode");
        } else {
            Serial.println("[WiFi] NVS open FAILED!");
        }
    }
    
    loadCredentials();
    Serial.println("[WiFi] Initialized");
}

bool WiFiManager::autoConnect() {
    if (hasSavedCredentials()) {
        Serial.println("[WiFi] Trying saved WiFi credentials...");
        
        for (int i = 0; i < credentialCount; i++) {
            Serial.printf("   Trying SSID %d: %s\n", i + 1, savedCredentials[i].ssid.c_str());
            
            WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
            WiFi.begin(savedCredentials[i].ssid.c_str(), savedCredentials[i].password.c_str());
            esp_wifi_set_max_tx_power(WIFI_TX_POWER_RAW);

            int8_t actualPower = 0;
            if (esp_wifi_get_max_tx_power(&actualPower) == ESP_OK) {
                Serial.printf("[WiFi] TX power set -> readback: %d (raw, 0.25 dBm/unit) ≈ %.2f dBm\n",
                              actualPower, actualPower * 0.25f);
            } else {
                Serial.println("[WiFi] Failed to read TX power");
            }
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 40) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[WiFi] Saved WiFi connected!");
                Serial.print("   IP: ");
                Serial.println(WiFi.localIP());
                return true;
            }
            
            Serial.println("\n[WiFi] SSID failed, trying next...");
            WiFi.disconnect(false);
            delay(100);
        }
    }
    
    Serial.print("[WiFi] Trying default WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    esp_wifi_set_max_tx_power(WIFI_TX_POWER_RAW);

    int8_t actualPower = 0;
    if (esp_wifi_get_max_tx_power(&actualPower) == ESP_OK) {
        Serial.printf("[WiFi] TX power set -> readback: %d (raw, 0.25 dBm/unit) ≈ %.2f dBm\n",
                      actualPower, actualPower * 0.25f);
    } else {
        Serial.println("[WiFi] Failed to read TX power");
    }
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Default WiFi connected!");
        Serial.print("   IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }
    
    Serial.println("\n[WiFi] Default WiFi failed!");
    WiFi.disconnect(false);
    delay(100);
    return false;
}

void WiFiManager::startConfigPortal() {
    Serial.println("[WiFi] Starting Config Portal...");
    
    configMode = true;
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32-Weather", NULL, 1, false, 4);
    
    delay(500);
    
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("[WiFi] AP SSID: ");
    Serial.println(WiFi.softAPSSID());
    
    startWebServer();
}

void WiFiManager::startWebServer() {
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
    
    webServer->onNotFound([this]() {
        handleNotFound();
    });
    
    webServer->begin();
    Serial.println("[WiFi] Web server started");
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
    Serial.println("[WiFi] Scanning networks...");
    
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
    
    Serial.println("[WiFi] Saving credentials...");
    Serial.print("   SSID: ");
    Serial.println(ssid);
    
    saveCredentials(ssid, password);
    
    webServer->send(200, "text/plain", "OK");
    
    delay(1000);
    ESP.restart();
}

void WiFiManager::handleForget() {
    String ssid = webServer->arg("ssid");
    
    Serial.println("[WiFi] Forgetting credentials...");
    Serial.print("   SSID: ");
    Serial.println(ssid);
    
    forgetCredentials(ssid);
    
    webServer->send(200, "text/plain", "OK");
}

void WiFiManager::handleNotFound() {
    webServer->send(404, "text/plain", "Not Found");
}

void WiFiManager::connectToWiFi(const char* ssid, const char* password, int timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(500);
        Serial.print(".");
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
    Serial.printf("[WiFi] Saved %d credentials to NVS\n", credentialCount);
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
            Serial.print("[WiFi] Migrated old SSID: ");
            Serial.println(oldSSID);
        }
        return credentialCount > 0;
    }
    
    for (int i = 0; i < credentialCount && i < MAX_WIFI_CREDENTIALS; i++) {
        String ssidKey = "ssid_" + String(i);
        String passKey = "pass_" + String(i);
        savedCredentials[i].ssid = preferences.getString(ssidKey.c_str(), "");
        savedCredentials[i].password = preferences.getString(passKey.c_str(), "");
    }
    
    Serial.printf("[WiFi] Loaded %d credentials\n", credentialCount);
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
    return "ESP32-WeatherClock";
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
    
    Serial.printf("[WiFi] Forgetting SSID, %d credentials remaining\n", credentialCount);
}

void WiFiManager::scanNetworks() {
    networksFound = WiFi.scanNetworks(false, false);
    scanComplete = true;
}

void WiFiManager::startSmartConfig() {
    if (smartConfigStarted) return;
    
    Serial.println("[SmartConfig] Starting ESP-Touch SmartConfig...");
    
    WiFi.beginSmartConfig();
    smartConfigStarted = true;
    smartConfigDone = false;
    smartConfigStartTime = millis();
    
    Serial.println("[SmartConfig] Waiting for ESP-Touch packet...");
}

void WiFiManager::stopSmartConfig() {
    if (!smartConfigStarted) return;
    
    Serial.println("[SmartConfig] Stopping SmartConfig...");
    
    WiFi.stopSmartConfig();
    smartConfigStarted = false;
    smartConfigDone = false;
    smartConfigStartTime = 0;
    
    Serial.println("[SmartConfig] Stopped");
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
        Serial.println("[SmartConfig] SmartConfig received!");
        smartConfigDone = true;
        
        String ssid = WiFi.SSID();
        String password = WiFi.psk();
        
        Serial.print("[SmartConfig] SSID: ");
        Serial.println(ssid);
        
        saveCredentials(ssid, password);
    }
    
    if (smartConfigStartTime > 0 && (millis() - smartConfigStartTime) >= SMART_CONFIG_TIMEOUT_MS) {
        Serial.println("[SmartConfig] Timeout (2 minutes), stopping SmartConfig...");
        stopSmartConfig();
    }
}