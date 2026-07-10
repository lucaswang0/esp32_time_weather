#include "WiFiManager.h"
#include "config.h"

WiFiManager::WiFiManager() : reconnectCount(0), initialized(false), _initialConnected(false) {
}

bool WiFiManager::connect() {
    Serial.println("[WiFi] Starting STA mode connection...");
    
    if (!initialized) {
        configManager.begin();
        initialized = true;
    }
    
    if (configManager.autoConnect()) {
        Serial.println("[WiFi] STA mode connected successfully!");
        reconnectCount = 0;
        _initialConnected = true;
        return true;
    }
    
    Serial.println("[WiFi] STA mode connection failed");
    return false;
}

void WiFiManager::maintainConnection() {
    if (configManager.isConfigMode()) {
        configManager.handleClient();
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        reconnectCount++;
        Serial.println("\n[WiFi] Disconnected! Reconnecting...");
        
        WiFi.disconnect();
        delay(100);
        
        if (configManager.autoConnect()) {
            reconnectCount = 0;
        } else {
            Serial.println("[WiFi] Reconnect failed, will retry later");
        }
    }
    
    configManager.handleClient();
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
    return configManager.isConfigMode();
}

void WiFiManager::handleClient() {
    configManager.handleClient();
}

void WiFiManager::startAPMode() {
    configManager.startAPMode();
}

void WiFiManager::stopAPMode() {
    configManager.stopAPMode();
}

bool WiFiManager::isAPStarted() {
    return configManager.isAPStarted();
}

void WiFiManager::checkAPTimeout() {
    if (!configManager.isAPStarted()) return;
    
    unsigned long apStartTime = configManager.getAPStartTime();
    if (apStartTime > 0 && (millis() - apStartTime) >= AP_TIMEOUT_MS) {
        Serial.println("[WiFi] AP mode timeout (10 minutes), stopping AP...");
        stopAPMode();
    }
}

bool WiFiManager::hasInitialConnected() const {
    return _initialConnected;
}