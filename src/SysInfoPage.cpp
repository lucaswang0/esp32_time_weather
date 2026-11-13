#include "SysInfoPage.h"
#include <esp_log.h>
#include <TFT_eSPI.h>
#include "font_small_20.h"
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
    unsigned long now = millis();
    
    if (_firstDraw) {
        _firstDraw = false;
        drawStaticContent();
    }
    
    if (now - _lastUpdateTime < 1000) {
        return;
    }
    _lastUpdateTime = now;
    
    updateDynamicContent();
}

void WiFiInfoPage::drawStaticContent() {
    // tft.drawString("连接状态:", 5, 5);
    _display.drawTextWithTransparentBg("WiFi名称:", 5, 5, TFT_WHITE);
    _display.drawTextWithTransparentBg("IP地址:", 5, 28, TFT_WHITE);
    _display.drawTextWithTransparentBg("DNS地址:", 5, 51, TFT_WHITE);
    _display.drawTextWithTransparentBg("信号强度:", 5, 74, TFT_WHITE);
    _display.drawTextWithTransparentBg("Min/Free/Total:", 5, 97, TFT_WHITE);
    _display.drawTextWithTransparentBg("Uptime:", 5, 120, TFT_WHITE);
    _display.drawTextWithTransparentBg("芯片温度:", 5, 143, TFT_WHITE);
    // tft.drawString("长按10秒进入AP配网", 5, 180);
        // 准备要显示的文字数组
    const char* chars[] = {"长按", "10秒", "进入", "AP", "配网"};
    int charCount = 5;
    
    int x = 275;        // 固定x坐标
    int startY = 30;     // 起始y坐标
    int spacing = 25;   // 间隔
    
    for (int i = 0; i < charCount; i++) {
        int y = startY + i * spacing;
        // 确保不超出170
        if (y > 170) break;
        _display.drawTextWithTransparentBg(chars[i], x, y, COLOR_GOLD_WARM);
    }
}

void WiFiInfoPage::updateDynamicContent() {
    bool connected = _wifi.isConnected();
    
    // tft.drawString(connected ? "已连接" : "未连接", 120, 5);
    
    _display.drawTextWithTransparentBg(_wifi.getSSID(), 120, 5, TFT_WHITE);
    
    _display.drawTextWithTransparentBg(_wifi.getLocalIP(), 120, 28, TFT_WHITE);
    _display.drawTextWithTransparentBg(_wifi.getdnsIP(), 120, 51, TFT_WHITE);
    
    // 清除该区域的旧内容（加一点边距防止边缘残留）
    // tft.fillRect(120, 105, strWidth + 5, 20, TFT_BLACK);
    // tft.drawString(rssiStr, 120, 105);

    // char reconnectStr[16];
    // snprintf(reconnectStr, sizeof(reconnectStr), "%d", _wifi.getReconnectCount());
    // tft.drawString(reconnectStr, 120, 130);
    
    // 系统运行时间（格式：1d 2h 3m 4s）
    unsigned long ms = millis();
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    unsigned long days = hrs / 24;
    char uptimeStr[24];
    snprintf(uptimeStr, sizeof(uptimeStr), "%lud %02lu:%02lu:%02lu",
             days, hrs % 24, mins % 60, secs % 60);
    _display.drawTextWithTransparentBgFont(uptimeStr, 135, 120, TFT_WHITE, font_small_20);

    // 最小剩余内存/内存可用/总内存
    uint32_t minFree = ESP.getMinFreeHeap();
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    char memStr[24];
    snprintf(memStr, sizeof(memStr), "%luK/%luK/%luK",
             minFree / 1024, freeHeap / 1024, totalHeap / 1024);
    _display.drawTextWithTransparentBgFont(memStr, 150, 97, TFT_WHITE, font_small_20);

    // 芯片内部温度
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.1fC", temperatureRead());
    _display.drawTextWithTransparentBgFont(tempStr, 130, 143, TFT_CYAN, font_small_20);

    char rssiStr[16];
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm", _wifi.getRSSI());
    _display.drawTextWithTransparentBgFont(rssiStr, 120, 74, TFT_WHITE, font_small_20);
}