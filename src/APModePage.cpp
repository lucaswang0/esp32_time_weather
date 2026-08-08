#include "APModePage.h"
#include <esp_log.h>
#include "config.h"

static const char* TAG = "APModePage";
APModePage::APModePage(DisplayManager& display, WiFiManager& wifi)
    : _display(display), _wifi(wifi), _apStartTime(0), _lastDrawTime(0), _firstDraw(true) {
}

void APModePage::onEnter() {
    ESP_LOGI(TAG, "onEnter");
    _apStartTime = millis();
    _lastDrawTime = 0;
    _firstDraw = true;
    
    auto& tft = _display.getTFT();
    drawStaticContent(tft);
}

void APModePage::onExit() {
    ESP_LOGI(TAG, "onExit");
    // 方案 A+D 配套: 退出 AP 页面时主动停止 AP 模式（不保存配置则关闭 AP）
    if (_wifi.isAPStarted()) {
        ESP_LOGI(TAG, "Stopping AP mode on exit");
        _wifi.stopAPMode();
    }
}

void APModePage::update() {
    auto& tft = _display.getTFT();
    unsigned long now = millis();

    if (now - _lastDrawTime < 1000) {
        return;
    }
    _lastDrawTime = now;

    // 方案 D: 倒计时 0 主动停止 AP 模式
    // WiFiManager::checkAPTimeout 只在 WiFi 已连接 + maintainConnection 时被调用,
    // AP 模式下永远不会触发, 所以这里手动检查并停止 AP
    if (!_wifi.isAPStarted() || getRemainingSeconds() == 0) {
        if (_wifi.isAPStarted()) {
            ESP_LOGI(TAG, "Countdown ended, stopping AP mode");
            _wifi.stopAPMode();
            // TaskManager 会在下一轮 WiFi 检查时发现 AP 关闭 + 当前在 AP_MODE
            // → 自动 switchTo(PAGE_TEMP)
        }
        return;  // 倒计时 0 后不再画 00:00
    }

    updateCountdown(tft);
}

void APModePage::drawStaticContent(TFT_eSPI& tft) {
    tft.fillScreen(TFT_BLACK);

    _display.drawTextXFont("AP配网", 160, 8, TFT_GREEN, TC_DATUM);
    _display.drawTextXFont("10:00", 315, 8, TFT_YELLOW, TR_DATUM);
    _display.drawTextXFont("1. 连接 WiFi: ESP32-Weather", 5, 32, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("2. 浏览器访问:", 5, 56, TFT_WHITE, TL_DATUM);

    int offsetX = _display.getXFontTextWidth("2. 浏览器访问:");
    _display.drawTextXFont("http://192.168.4.1", 5 + offsetX, 56, TFT_CYAN, TL_DATUM);

    _display.drawTextXFont("3. 选择WiFi并输入密码", 5, 80, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("4. 保存后设备将重启", 5, 104, TFT_WHITE, TL_DATUM);
    _display.drawTextXFont("也使用EspTouch工具配置", 5, 128, TFT_CYAN, TL_DATUM);
    _display.drawTextXFont("未配置则倒计时结束后返回", 5, 152, TFT_ORANGE, TL_DATUM);
}

void APModePage::updateCountdown(TFT_eSPI& tft) {
    int remaining = getRemainingSeconds();
    int minutes = remaining / 60;
    int seconds = remaining % 60;

    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);

    tft.fillRect(240, 0, 80, 24, TFT_BLACK);
    _display.drawTextXFont(timeStr, 315, 8, TFT_YELLOW, TR_DATUM);
}

int APModePage::getRemainingSeconds() {
    if (!_wifi.isAPStarted()) return 0;
    unsigned long elapsed = millis() - _wifi.getAPStartTime();
    if (elapsed >= AP_TIMEOUT_MS) return 0;
    return (AP_TIMEOUT_MS - elapsed) / 1000;
}