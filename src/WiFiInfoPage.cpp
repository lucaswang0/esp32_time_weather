#include "WiFiInfoPage.h"
#include <esp_log.h>
#include <TFT_eSPI.h>
#include "DisplayManager.h"


static const char* TAG = "WiFiInfoPage";
WiFiInfoPage::WiFiInfoPage(DisplayManager& display, WiFiManager& wifi)
    : _display(display), _wifi(wifi), _firstDraw(true), _lastUpdateTime(0) {
}

void WiFiInfoPage::onEnter() {
    ESP_LOGI(TAG, "onEnter");
    _display.clearScreen();
    _firstDraw = true;
    _lastUpdateTime = 0;
}

void WiFiInfoPage::onExit() {
    ESP_LOGI(TAG, "onExit");
}

void WiFiInfoPage::update() {
    auto& tft = _display.getTFT();
    unsigned long now = millis();
    
    if (_firstDraw) {
        _firstDraw = false;
        drawStaticContent(tft);
    }
    
    if (now - _lastUpdateTime < 1000) {
        return;
    }
    _lastUpdateTime = now;
    
    updateDynamicContent(tft);
}

void WiFiInfoPage::drawStaticContent(TFT_eSPI& tft) {
    _display.drawTextXFont("连接状态:", 5, 5, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("WiFi名称:", 5, 30, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("IP地址:", 5, 55, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("DNS地址:", 5, 80, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("信号强度:", 5, 105, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("重连次数:", 5, 130, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("长按10秒进入AP配网", 5, 155, COLOR_GOLD_WARM, TL_DATUM);
}

void WiFiInfoPage::updateDynamicContent(TFT_eSPI& tft) {
    bool connected = _wifi.isConnected();

    _display.drawTextXFont(connected ? "已连接" : "未连接", 120, 5,
                           connected ? TFT_GREEN : TFT_RED, TL_DATUM);
    _display.drawTextXFont(_wifi.getSSID(), 120, 30, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont(_wifi.getLocalIP(), 120, 55, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont(_wifi.getdnsIP(), 120, 80, TFT_WHITE, TL_DATUM);

    char rssiStr[16];
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm", _wifi.getRSSI());
    _display.drawTextWithTransparentBg(rssiStr, 120, 105, TFT_WHITE);

    char reconnectStr[16];
    snprintf(reconnectStr, sizeof(reconnectStr), "%d", _wifi.getReconnectCount());
    _display.drawTextXFont(reconnectStr, 120, 130, TFT_WHITE, TL_DATUM);
}