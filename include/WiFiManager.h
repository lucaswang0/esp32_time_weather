#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include "WiFiConfigManager.h"

class WiFiManager {
public:
    WiFiManager();
    bool connect();
    void maintainConnection();
    bool isConnected();
    int getReconnectCount();
    int getRSSI();
    const char* getLocalIP();
    const char* getSSID();
    bool isAPMode();
    void handleClient();
    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
    void checkAPTimeout();
    bool hasInitialConnected() const;
    
private:
    WiFiConfigManager configManager;
    int reconnectCount;
    bool initialized;
    bool _initialConnected;
    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000; // 10分钟
};

#endif