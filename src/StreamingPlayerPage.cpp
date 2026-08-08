#include "StreamingPlayerPage.h"
#include "DisplayManager.h"
#include <lwip/sockets.h>
#include <esp_log.h>

#define SERVER_HOST "localpc.com"
#define SERVER_PORT 8888

static const char* TAG = "STREAM";

StreamingPlayerPage::StreamingPlayerPage(DisplayManager& display) : _display(display) {
    _lastFpsUpdateTime = millis();
    _frameCount = 0;
    _currentFps = 0.0f;
    strcpy(_fpsText, "帧率:0.0");
}

StreamingPlayerPage::~StreamingPlayerPage() {
    if (_rawBuf) {
        delete[] _rawBuf;
        _rawBuf = nullptr;
    }
    if (_tcpClient.connected()) {
        _tcpClient.stop();
    }
}

void StreamingPlayerPage::onEnter() {
    _state = ST_CONNECTING;
    _rdPhase = 0;
    _rdPos = 0;
    _lastFrameT = 0;
    _lastConnectAttempt = 0;
    _connectionFailureCount = 0;
    _lastFpsUpdateTime = millis();
    _frameCount = 0;
    _currentFps = 0.0f;
    strcpy(_fpsText, "帧率:0.0");
    
    if (!_rawBuf) {
        _rawBuf = new uint8_t[BUFFER_SIZE];
    }
    
    auto& tft = _display.getTFT();
    tft.fillScreen(TFT_BLACK);
    
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGI(TAG, "No WiFi");
        _state = ST_ERROR;
        drawErrorScreen("请先连接WiFi");
        return;
    }
    
    drawConnectingScreen();
}

void StreamingPlayerPage::onExit() {
    if (_tcpClient.connected()) {
        _tcpClient.stop();
    }
    if (_rawBuf) {
        delete[] _rawBuf;
        _rawBuf = nullptr;
    }
    _display.clearScreen();
}

void StreamingPlayerPage::drawConnectingScreen() {
    auto& tft = _display.getTFT();
    tft.fillScreen(TFT_BLACK);

    _display.drawTextXFont("正在连接流媒体服务器...", 2, 60, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont(SERVER_HOST, 160, 95, COLOR_GRAY_DARK, TL_DATUM);
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "端口:%d", SERVER_PORT);
    _display.drawTextXFont(portStr, 160, 115, COLOR_GRAY_DARK, TL_DATUM);
    _display.drawTextXFont("触摸返回", 160, 146, COLOR_GRAY_MID, TL_DATUM);
}

void StreamingPlayerPage::drawErrorScreen(const char* msg) {
    auto& tft = _display.getTFT();
    tft.fillScreen(TFT_BLACK);

    _display.drawTextXFont(msg, 160, 70, TFT_WHITE, TC_DATUM);
    _display.drawTextXFont("双击重试", 160, 146, COLOR_GRAY_MID, TC_DATUM);
}

void StreamingPlayerPage::drawFps() {
    // 在屏幕右上角绘制FPS，黑色半透明背景
    auto& tft = _display.getTFT();
    // 背景矩形（黑色，覆盖之前的内容）
    tft.fillRect(320 - 60, 0, 60, 18, TFT_BLACK);
    _display.drawTextXFont(_fpsText, 320 - 2, 2, TFT_GREEN, TR_DATUM);
}

bool StreamingPlayerPage::connectToServer() {
    if (_tcpClient.connected()) {
        _tcpClient.stop();
    }

    // 等待 socket 完全释放（避免 TIME_WAIT 状态冲突）
    delay(100);

    ESP_LOGI(TAG, "Connecting to %s:%d...", SERVER_HOST, SERVER_PORT);

    if (!_tcpClient.connect(SERVER_HOST, SERVER_PORT, 5000)) {
        ESP_LOGI(TAG, "Connection failed");
        _tcpClient.stop();  // 清理 socket
        return false;
    }
    
    _tcpClient.setNoDelay(true);
    _tcpClient.setTimeout(5);
    
    int rcvbuf = 65536;
    setsockopt(_tcpClient.fd(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    _rdPhase = 0;
    _rdPos = 0;
    _lastFrameT = millis();
    
    ESP_LOGI(TAG, "Connected successfully");
    return true;
}

void StreamingPlayerPage::update() {
    unsigned long n = millis();
    
    if (_state == ST_CONNECTING) {
        // 重试退避：失败次数越多，等待时间越长（5-30秒）
        unsigned long backoff = min(30000UL, 5000UL * (1UL << min((unsigned long)_connectionFailureCount, 4UL)));
        if (n - _lastConnectAttempt < backoff) {
            return;
        }
        _lastConnectAttempt = n;
        
        if (connectToServer()) {
            ESP_LOGI(TAG, "Connected, entering PLAYING state");
            _state = ST_PLAYING;
            _connectionFailureCount = 0;
        } else {
            _connectionFailureCount++;
            ESP_LOGW(TAG, "Connection attempt %d/%d failed", _connectionFailureCount, MAX_CONNECTION_FAILURES);
            
            if (_connectionFailureCount >= MAX_CONNECTION_FAILURES) {
                _state = ST_ERROR;
                drawErrorScreen("无法连接服务器");
            }
        }
        return;
    }
    
    if (_state == ST_PLAYING) {
        if (!_tcpClient.connected()) {
            ESP_LOGI(TAG, "Connection lost");
            _state = ST_CONNECTING;
            _lastConnectAttempt = 0;
            drawConnectingScreen();
            return;
        }

        // 调试：每5秒输出一次连接状态
        static unsigned long lastDebugLog = 0;
        if (n - lastDebugLog > 5000) {
            lastDebugLog = n;
            // Serial.printf("[STREAM][DEBUG] Status: connected=%d, available=%d, time_since_last_data=%lu ms\n",
                        //   _tcpClient.connected(), _tcpClient.available(), n - _lastFrameT);
        }
        
        auto& tft = _display.getTFT();
        unsigned long loopStart = n;
        
        while (_tcpClient.available() > 0) {
            if (millis() - loopStart > 50) {
                break;
            }
            
            if (_rdPhase == 0) {
                size_t available = _tcpClient.available();
                size_t to_read = min(available, (size_t)(HEADER_SIZE - _rdPos));
                size_t bytes_read = _tcpClient.read(_rawBuf + _rdPos, to_read);
                
                if (bytes_read <= 0) {
                    break;
                }
                
                _rdPos += bytes_read;
                
                if (_rdPos >= HEADER_SIZE) {
                    _frameX = ((uint16_t)_rawBuf[0] << 8) | _rawBuf[1];
                    _frameY = ((uint16_t)_rawBuf[2] << 8) | _rawBuf[3];
                    _frameW = ((uint16_t)_rawBuf[4] << 8) | _rawBuf[5];
                    _frameH = ((uint16_t)_rawBuf[6] << 8) | _rawBuf[7];
                    _dataLen = ((uint32_t)_rawBuf[8] << 24) |
                               ((uint32_t)_rawBuf[9] << 16) |
                               ((uint32_t)_rawBuf[10] << 8) |
                               _rawBuf[11];

                    // 检测心跳包 (x=0xFFFF)
                    if (_frameX == 0xFFFF && _frameY == 0xFFFF && _dataLen == 0) {
                        _lastFrameT = n;  // 更新最后帧时间，防止超时
                        _rdPhase = 0;
                        _rdPos = 0;
                        // Serial.println("[STREAM][DEBUG] Heartbeat received");
                        continue;
                    }

                    if (_dataLen > MAX_CHUNK_SIZE) {
                        ESP_LOGE(TAG, "Error: DataLen(%u) exceeds buffer size(%u)",
                                      _dataLen, MAX_CHUNK_SIZE);
                        _tcpClient.stop();
                        _state = ST_CONNECTING;
                        _lastConnectAttempt = 0;
                        drawConnectingScreen();
                        return;
                    }
                    
                    _rdPhase = 1;
                    _rdPos = 0;
                }
            } else if (_rdPhase == 1) {
                size_t available = _tcpClient.available();
                size_t to_read = min(available, (size_t)(_dataLen - _rdPos));
                size_t bytes_read = _tcpClient.read(_rawBuf + _rdPos, to_read);
                
                if (bytes_read <= 0) {
                    break;
                }
                
                _rdPos += bytes_read;
                
                if (_rdPos >= _dataLen) {
                    _lastFrameT = n;
                    _frameCount++;
                    // 每秒更新一次FPS
                    if (n - _lastFpsUpdateTime >= 1000) {
                        _currentFps = _frameCount * 1000.0f / (n - _lastFpsUpdateTime);
                        _frameCount = 0;
                        _lastFpsUpdateTime = n;
                        snprintf(_fpsText, sizeof(_fpsText), "帧率:%.1f", _currentFps);
                    }
                    if (_frameW > 0 && _frameH > 0 && _frameX < 320 && _frameY < 170) {
                        tft.setSwapBytes(true);
                        tft.pushImage(_frameX, _frameY, _frameW, _frameH, (uint16_t*)_rawBuf);
                        tft.setSwapBytes(false);
                        drawFps(); // 绘制FPS
                    }
                    _rdPhase = 0;
                    _rdPos = 0;
                }
            }
        }
        
        if (n - _lastFrameT > 15000) {
            // Serial.printf("[STREAM][DEBUG] RX timeout: _lastFrameT=%lu, n=%lu, diff=%lu ms, available=%d, connected=%d\n",
                          //   _lastFrameT, n, n - _lastFrameT, _tcpClient.available(), _tcpClient.connected());
            _tcpClient.stop();
            _state = ST_CONNECTING;
            _lastConnectAttempt = 0;
            drawConnectingScreen();
        }
        
        return;
    }
}

void StreamingPlayerPage::onTouch(PageTouchType type) {
    if (type == TOUCH_SHORT_BASE) {
        ESP_LOGI(TAG, "Touch - exiting");
        return;
    }
    
    if (type == TOUCH_DOUBLE_BASE && (_state == ST_ERROR || _state == ST_CONNECTING)) {
        ESP_LOGI(TAG, "Double touch - retrying");
        _state = ST_CONNECTING;
        _connectionFailureCount = 0;
        _lastConnectAttempt = 0;
        drawConnectingScreen();
    }
}