#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#define MAX_WIFI_CREDENTIALS 5

typedef struct {
    String ssid;
    String password;
} WiFiCredential;

class WiFiConfigManager {
public:
    WiFiConfigManager();
    void begin();
    bool autoConnect();
    void startConfigPortal();
    void scanNetworks();
    void saveCredentials(const String& ssid, const String& password);
    bool loadCredentials();
    bool hasSavedCredentials();
    IPAddress getIP();
    bool isConfigMode();
    const char* getConfigSSID();
    const char* getConfigPassword();
    void handleClient();
    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
    unsigned long getAPStartTime();
    const WiFiCredential* getSavedCredentials() const;
    int getCredentialCount() const;
    void forgetCredentials(const String& ssid);
    
private:
    void startWebServer();
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleForget();
    void handleNotFound();
    void connectToWiFi(const char* ssid, const char* password, int timeoutMs);

    Preferences preferences;
    WebServer* webServer;
    bool configMode;
    WiFiCredential savedCredentials[MAX_WIFI_CREDENTIALS];
    int credentialCount;
    bool scanComplete;
    int8_t networksFound;
    bool apStarted;
    unsigned long apStartTime;
};

#endif