#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>

#define MAX_WIFI_CREDENTIALS 5

typedef struct {
    String ssid;
    String password;
} WiFiCredential;

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
    unsigned long getAPStartTime();
    const WiFiCredential* getSavedCredentials() const;
    int getCredentialCount() const;
    void forgetCredentials(const String& ssid);
    
    void startSmartConfig();
    void stopSmartConfig();
    bool isSmartConfigStarted();
    bool isSmartConfigDone();
    
private:
    void startWebServer();
    void stopWebServer();
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleForget();
    void handleNotFound();
    void connectToWiFi(const char* ssid, const char* password, int timeoutMs);
    void handleSmartConfig();
    
    // SPIFFS 文件管理相关
    void startFSWebServer();
    static String sizeStr(uint64_t bytes);
    void handleFS();
    void handleDownload();
    void handleUpload();
    void handleUploadEnd();
    void handleDelete();

    Preferences preferences;
    WebServer* webServer;
    bool configMode;
    bool fsServerStarted;
    fs::File uploadFile;
    WiFiCredential savedCredentials[MAX_WIFI_CREDENTIALS];
    int credentialCount;
    bool scanComplete;
    int8_t networksFound;
    bool apStarted;
    unsigned long apStartTime;
    int reconnectCount;
    bool initialized;
    bool _initialConnected;
    bool smartConfigStarted;
    bool smartConfigDone;
    unsigned long smartConfigStartTime;
    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;
    static const unsigned long SMART_CONFIG_TIMEOUT_MS = 120 * 1000;
};

#endif