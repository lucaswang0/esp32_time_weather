#ifndef STREAMING_PLAYER_PAGE_H
#define STREAMING_PLAYER_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include <WiFi.h>
#include <WiFiUdp.h>

class StreamingPlayerPage : public PageBase {
public:
    StreamingPlayerPage(DisplayManager& display);
    ~StreamingPlayerPage();
    
    void onEnter() override;
    void onExit() override;
    void update() override;
    void onTouch(PageTouchType type) override;

private:
    DisplayManager& _display;

    static constexpr uint16_t SERVER_PORT = 8888;
    static constexpr uint16_t BROADCAST_PORT = 8889;  // PC 监听广播的 UDP 端口
    static constexpr uint16_t BROADCAST_INTERVAL_MS = 2000;  // 广播间隔（毫秒）

    static const int HEADER_SIZE = 12;
    static const int MAX_CHUNK_SIZE = 8192;
    static const int BUFFER_SIZE = HEADER_SIZE + MAX_CHUNK_SIZE;
    
    enum State { ST_LISTENING, ST_CONNECTED, ST_PLAYING, ST_ERROR };
    State _state = ST_LISTENING;
    
    WiFiServer _tcpServer;
    WiFiClient _tcpClient;
    WiFiUDP _udp;                         // UDP 广播实例
    unsigned long _lastBroadcastT = 0;    // 上次广播时间戳
    uint8_t* _rawBuf = nullptr;
    int _rdPhase = 0;
    int _rdPos = 0;
    unsigned long _lastFrameT = 0;
    unsigned long _lastConnectAttempt = 0;
    unsigned long _lastClientDisconnectT = 0;
    int _connectionFailureCount = 0;
    const int MAX_CONNECTION_FAILURES = 999999;
    
    uint16_t _frameX = 0;
    uint16_t _frameY = 0;
    uint16_t _frameW = 0;
    uint16_t _frameH = 0;
    uint32_t _dataLen = 0;
    
    // FPS计算相关
    unsigned long _lastFpsUpdateTime = 0;
    unsigned long _frameCount = 0;
    float _currentFps = 0.0f;
    char _fpsText[16];

    bool listenForClient();
    void drawConnectingScreen();
    void drawErrorScreen(const char* msg);
    void drawFps();
    void sendBroadcast();        // 发送 UDP 广播包（仅在未连接时调用）
    void stopBroadcast();        // 停止广播（连接成功后停止）
};

#endif
