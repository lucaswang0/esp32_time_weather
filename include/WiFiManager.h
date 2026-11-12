#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>

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
    const char* getdnsIP();
    const char* getSSID();
    bool isAPMode();
    void handleClient();
    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
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
    void startPortalDns();       // 启动 Captive Portal 的 DNS 劫持
    void stopPortalDns();
    void updateApSsid();         // 按 MAC 末两字节生成热点名 esp32-xxxx
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleForget();
    void handleNotFound();
    void connectToWiFi(const char* ssid, const char* password, int timeoutMs);
    void handleSmartConfig();
    void startConfigMode();      // 进入配网模式（先 SmartConfig 再 AP 门户）
    void handleConfigMode();     // 配网状态机：阶段切换 + 10 分钟超时禁用
    void handleReconnect();      // 断线退避重连（5 次 ×10s，然后等 5 分钟循环）
    bool connectSavedIndex(int idx, unsigned long timeoutMs);  // 连接指定索引的已存凭据
    void onConnected();          // 置位联网就绪标志并复位重连计数
    
    // LittleFS 文件管理相关
    void startFSWebServer();
    static String sizeStr(uint64_t bytes);
    void handleFS();
    void handleDownload();
    void handleUpload();
    void handleUploadEnd();
    void handleDelete();

    Preferences preferences;
    WebServer* webServer;
    DNSServer dnsServer;
    bool dnsStarted;
    String apSsid;               // 热点名：esp32-<MAC 末两字节>
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
    // 配网模式：先 SmartConfig（30s 无密码则停止）再 AP 门户，总窗口 10 分钟后关闭并禁用至重启
    static const unsigned long SMART_CONFIG_TIMEOUT_MS = 30 * 1000UL;
    static const unsigned long CONFIG_MODE_TIMEOUT_MS = 10 * 60 * 1000UL;
    static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20 * 1000UL;
    unsigned long _configModeStartTime; // 配网窗口起点
    bool _configDisabled;               // 配网超时后禁用（只能重启后再配网）

    // 断线重连退避：5 次 ×10s 失败后等待 5 分钟再循环，避免射频满载发热
    static const int RECONNECT_RETRY_MAX = 5;
    static const unsigned long RECONNECT_RETRY_INTERVAL_MS = 10 * 1000UL;
    static const unsigned long RECONNECT_WAIT_MS = 5 * 60 * 1000UL;
    int _reconnectAttempts;
    unsigned long _reconnectWaitUntil;

    // 联网就绪标志：未连接 = false，阻止其它联网应用访问网络（isConnected() 返回该标志）
    bool _netReady;
    int _activeCredIndex;               // 上次连接成功的凭据索引
};

#endif
