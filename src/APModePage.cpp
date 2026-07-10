#include "APModePage.h"
#include "config.h"
#include "font_small_20.h"

APModePage::APModePage(DisplayManager& display, WiFiManager& wifi)
    : _display(display), _wifi(wifi), _apStartTime(0), _lastDrawTime(0), _firstDraw(true) {
}

void APModePage::onEnter() {
    Serial.println("[APModePage] onEnter");
    _apStartTime = millis();
    _lastDrawTime = 0;
    _firstDraw = true;
    
    auto& tft = _display.getTFT();
    drawStaticContent(tft);
}

void APModePage::onExit() {
    Serial.println("[APModePage] onExit");
}

void APModePage::update() {
    auto& tft = _display.getTFT();
    unsigned long now = millis();
    
    if (now - _lastDrawTime < 1000) {
        return;
    }
    _lastDrawTime = now;
    
    updateCountdown(tft);
}

void APModePage::drawStaticContent(TFT_eSPI& tft) {
    tft.fillScreen(TFT_BLACK);
    
    tft.loadFont(font_small_20);
    
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("AP配网", 160, 8);
    
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("10:00", 315, 8);
    
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("1. 连接 WiFi: ESP32-Weather", 5, 32);
    tft.drawString("2. 浏览器访问:", 5, 56);
    
    tft.setTextColor(TFT_CYAN);
    tft.drawString("   http://192.168.4.1", 5, 80);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString("3. 选择WiFi并输入密码", 5, 104);
    tft.drawString("4. 保存后设备将重启", 5, 128);
    
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("未配置则倒计时结束后返回", 5, 152);
    
    tft.unloadFont();
}

void APModePage::updateCountdown(TFT_eSPI& tft) {
    int remaining = getRemainingSeconds();
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);
    
    tft.loadFont(font_small_20);
    tft.setTextDatum(TR_DATUM);
    
    tft.fillRect(240, 0, 80, 24, TFT_BLACK);
    
    tft.setTextColor(TFT_YELLOW);
    tft.drawString(timeStr, 315, 8);
    
    tft.unloadFont();
}

int APModePage::getRemainingSeconds() {
    if (!_wifi.isAPStarted()) return 0;
    unsigned long elapsed = millis() - _apStartTime;
    if (elapsed >= AP_TIMEOUT_MS) return 0;
    return (AP_TIMEOUT_MS - elapsed) / 1000;
}