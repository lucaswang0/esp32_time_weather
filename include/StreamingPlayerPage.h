#ifndef STREAMING_PLAYER_PAGE_H
#define STREAMING_PLAYER_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include <WiFi.h>

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
    
    static const int HEADER_SIZE = 12;
    static const int MAX_CHUNK_SIZE = 8192;
    static const int BUFFER_SIZE = HEADER_SIZE + MAX_CHUNK_SIZE;
    
    enum State { ST_CONNECTING, ST_PLAYING, ST_ERROR };
    State _state = ST_CONNECTING;
    
    WiFiClient _tcpClient;
    uint8_t* _rawBuf = nullptr;
    int _rdPhase = 0;
    int _rdPos = 0;
    unsigned long _lastFrameT = 0;
    unsigned long _lastConnectAttempt = 0;
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

    bool connectToServer();
    void drawConnectingScreen();
    void drawErrorScreen(const char* msg);
    void drawFps();  // 绘制FPS
};

#endif