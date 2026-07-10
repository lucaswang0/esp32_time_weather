#include "WiFiInfoPage.h"
#include <TFT_eSPI.h>
#include "font_small_20.h"

WiFiInfoPage::WiFiInfoPage(DisplayManager& display, WiFiManager& wifi)
    : _display(display), _wifi(wifi), _firstDraw(true), _lastUpdateTime(0) {
}

void WiFiInfoPage::onEnter() {
    Serial.println("[WiFiInfoPage] onEnter");
    _display.clearScreen();
    _firstDraw = true;
    _lastUpdateTime = 0;
}

void WiFiInfoPage::onExit() {
    Serial.println("[WiFiInfoPage] onExit");
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
    tft.loadFont(font_small_20);
    tft.setTextDatum(TL_DATUM);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString("网络信息", 5, 5);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString("连接状态:", 5, 30);
    tft.drawString("WiFi名称:", 5, 55);
    tft.drawString("IP地址:", 5, 80);
    tft.drawString("信号强度:", 5, 105);
    tft.drawString("重连次数:", 5, 130);
    
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString("长按10秒进入AP配网", 5, 155);
    
    tft.unloadFont();
}

void WiFiInfoPage::updateDynamicContent(TFT_eSPI& tft) {
    tft.loadFont(font_small_20);
    tft.setTextDatum(TL_DATUM);
    
    bool connected = _wifi.isConnected();
    
    tft.setTextColor(connected ? TFT_GREEN : TFT_RED);
    tft.drawString(connected ? "已连接" : "未连接", 120, 30);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(_wifi.getSSID(), 120, 55);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(_wifi.getLocalIP(), 120, 80);
    
    tft.setTextColor(TFT_WHITE);
    char rssiStr[16];
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm", _wifi.getRSSI());
    tft.drawString(rssiStr, 120, 105);
    
    tft.setTextColor(TFT_WHITE);
    char reconnectStr[16];
    snprintf(reconnectStr, sizeof(reconnectStr), "%d", _wifi.getReconnectCount());
    tft.drawString(reconnectStr, 120, 130);
    
    tft.unloadFont();
}