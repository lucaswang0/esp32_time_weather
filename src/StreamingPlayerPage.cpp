#include "StreamingPlayerPage.h"
#include "font_small_20.h"
#include "DisplayManager.h"
#include <lwip/sockets.h>
#include <esp_log.h>

static const char* TAG = "STREAM";

StreamingPlayerPage::StreamingPlayerPage(DisplayManager& display)
    : _display(display), _tcpServer(SERVER_PORT) {
}

StreamingPlayerPage::~StreamingPlayerPage() {
    if (_rawBuf) {
        delete[] _rawBuf;
        _rawBuf = nullptr;
    }
    if (_tcpClient.connected()) {
        _tcpClient.stop();
    }
    _tcpServer.stop();
}

void StreamingPlayerPage::onEnter() {
    _state = ST_LISTENING;
    _rdPhase = 0;
    _rdPos = 0;
    _lastFrameT = 0;
    _lastConnectAttempt = 0;
    _lastClientDisconnectT = millis();
    _connectionFailureCount = 0;
    _hasConnectedOnce = false;
    _overlayVisible = false;
    _lastOverlayToggleT = 0;

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

    // 启动 TCP Server 监听 PC 客户端连接
    _tcpServer.begin();
    _tcpServer.setNoDelay(true);
    ESP_LOGI(TAG, "TCP Server listening on port %d", SERVER_PORT);
    
    // 开始发送 UDP 广播以供 PC 自动发现
    _udp.begin(BROADCAST_PORT);
    _lastBroadcastT = 0;  // 触发首次立即发送
    ESP_LOGI(TAG, "UDP broadcast started on port %d", BROADCAST_PORT);
    drawConnectingScreen();
}

void StreamingPlayerPage::onExit() {
    if (_tcpClient.connected()) {
        _tcpClient.stop();
    }
    _tcpServer.stop();
    _udp.stop();   // 停止 UDP 广播
    if (_rawBuf) {
        delete[] _rawBuf;
        _rawBuf = nullptr;
    }
    _display.clearScreen();
}

void StreamingPlayerPage::drawConnectingScreen() {
    auto& tft = _display.getTFT();
    tft.fillScreen(TFT_BLACK);

    tft.loadFont(font_small_20);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("等待PC客户端连接...", 2, 60);

    tft.setTextColor(COLOR_GRAY_DARK);
    tft.drawString(WiFi.localIP().toString().c_str(), 160, 95);
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "端口:%d", SERVER_PORT);
    tft.drawString(portStr, 160, 115);

    tft.setTextColor(COLOR_GRAY_MID);
    tft.drawString("触摸返回", 160, 146);

    tft.unloadFont();
}

void StreamingPlayerPage::drawErrorScreen(const char* msg) {
    auto& tft = _display.getTFT();
    tft.fillScreen(TFT_BLACK);

    tft.loadFont(font_small_20);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(msg, 160, 70);

    tft.setTextColor(COLOR_GRAY_MID);
    tft.drawString("双击重试", 160, 146);

    tft.unloadFont();
}

void StreamingPlayerPage::drawDisconnectedOverlay(bool visible) {
    // 不清屏，保留最后一帧画面，仅在屏幕中间叠加"连接中断..."
    // visible=false 时用黑色矩形擦除文字区域，实现闪烁
    auto& tft = _display.getTFT();
    tft.loadFont(font_small_20);
    tft.setTextDatum(MC_DATUM);
    int w = tft.textWidth("连接中断...");
    int h = tft.fontHeight();
    if (visible) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("连接中断...", 160, 85);
    } else {
        tft.fillRect(160 - w / 2 - 2, 85 - h / 2 - 2, w + 4, h + 4, TFT_BLACK);
    }
    tft.unloadFont();
}

void StreamingPlayerPage::sendBroadcast() {
    unsigned long n = millis();
    // 仅在未连接（ST_LISTENING）时发送广播
    if (_state != ST_LISTENING) return;
    
    if (n - _lastBroadcastT >= BROADCAST_INTERVAL_MS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "ESP32:%s:%d", 
                 WiFi.localIP().toString().c_str(), SERVER_PORT);
        
        // 向整个网段广播（255.255.255.255）
        IPAddress broadcastIP(255, 255, 255, 255);
        _udp.beginPacket(broadcastIP, BROADCAST_PORT);
        _udp.print(msg);
        _udp.endPacket();
        
        ESP_LOGD(TAG, "Broadcast sent: %s", msg);
        _lastBroadcastT = n;
    }
}

void StreamingPlayerPage::stopBroadcast() {
    // 广播在连接成功后由 _state 变为 ST_PLAYING 自动停止
    // 也可主动停止：_udp.stop();
    ESP_LOGI(TAG, "Broadcast stopped (client connected)");
    _lastBroadcastT = 0;  // 停止后重置，方便重连后重新触发
}

bool StreamingPlayerPage::listenForClient() {
    if (_tcpClient.connected()) {
        return true;
    }

    WiFiClient newClient = _tcpServer.available();
    if (newClient) {
        if (_tcpClient.connected()) {
            _tcpClient.stop();
        }
        _tcpClient = newClient;
        _tcpClient.setNoDelay(true);
        _tcpClient.setTimeout(5);

        int rcvbuf = 65536;
        setsockopt(_tcpClient.fd(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        ESP_LOGI(TAG, "Client connected: %s", _tcpClient.remoteIP().toString().c_str());

        _rdPhase = 0;
        _rdPos = 0;
        _lastFrameT = millis();
        _state = ST_PLAYING;
        _overlayVisible = false;
        // 重连时清屏，移除上一帧及"连接中断..."叠加层
        if (_hasConnectedOnce) {
            _display.getTFT().fillScreen(TFT_BLACK);
        }
        _hasConnectedOnce = true;
        // 连接后停止广播
        ESP_LOGI(TAG, "连接后调用 stopBroadcast");
        stopBroadcast();
        _connectionFailureCount = 0;
        return true;
    }
    return false;
}

void StreamingPlayerPage::update() {
    unsigned long n = millis();

    if (_state == ST_LISTENING) {
        sendBroadcast();
        if (listenForClient()) {
            ESP_LOGI(TAG, "Client accepted, entering PLAYING state");
        }
        // 断线后"连接中断..."每秒闪烁一次（500ms亮 / 500ms灭）
        if (_hasConnectedOnce && n - _lastOverlayToggleT >= 500) {
            _lastOverlayToggleT = n;
            _overlayVisible = !_overlayVisible;
            drawDisconnectedOverlay(_overlayVisible);
        }
        return;
    }

    if (_state == ST_PLAYING) {
        if (!_tcpClient.connected()) {
            ESP_LOGI(TAG, "Client disconnected");
            _tcpClient.stop();
            _state = ST_LISTENING;
            _lastClientDisconnectT = millis();
            _overlayVisible = true;
            _lastOverlayToggleT = n;
            drawDisconnectedOverlay(true);
            return;
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
                        _lastFrameT = n;
                        _rdPhase = 0;
                        _rdPos = 0;
                        continue;
                    }

                    if (_dataLen > MAX_CHUNK_SIZE) {
                        ESP_LOGE(TAG, "Error: DataLen(%u) exceeds buffer size(%u)",
                                      _dataLen, MAX_CHUNK_SIZE);
                        _tcpClient.stop();
                        _state = ST_LISTENING;
                        _lastClientDisconnectT = millis();
                        _overlayVisible = true;
                        _lastOverlayToggleT = n;
                        drawDisconnectedOverlay(true);
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
                    if (_frameW > 0 && _frameH > 0 && _frameX < 320 && _frameY < 170) {
                        tft.setSwapBytes(true);
                        tft.pushImage(_frameX, _frameY, _frameW, _frameH, (uint16_t*)_rawBuf);
                        tft.setSwapBytes(false);
                    }
                    _rdPhase = 0;
                    _rdPos = 0;
                }
            }
        }

        if (n - _lastFrameT > 15000) {
            ESP_LOGW(TAG, "RX timeout, closing client");
            _tcpClient.stop();
            _state = ST_LISTENING;
            _lastClientDisconnectT = millis();
            _overlayVisible = true;
            _lastOverlayToggleT = n;
            drawDisconnectedOverlay(true);
        }

        return;
    }
}

void StreamingPlayerPage::onTouch(PageTouchType type) {
    if (type == TOUCH_SHORT_BASE) {
        ESP_LOGI(TAG, "Touch - exiting");
        return;
    }

    if (type == TOUCH_DOUBLE_BASE && (_state == ST_ERROR || _state == ST_LISTENING)) {
        ESP_LOGI(TAG, "Double touch - retrying listen");
        _state = ST_LISTENING;
        _connectionFailureCount = 0;
        _lastClientDisconnectT = 0;
        drawConnectingScreen();
    }
}
