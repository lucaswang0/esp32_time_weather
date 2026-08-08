#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include <Arduino.h>
#include <stdlib.h>
#include <WiFi.h>
#include <PNGdec.h>
#include "font_large_72.h"
#include "font_medium_32.h"
#include "xfont.h"
#include "config.h"
#include "bg4.h"
#include "bg6.h"
#include "bg8.h"

extern const int BACKLIGHT_CHANNEL;

#define COLOR_TRANSPARENT  0x0001
#define COLOR_AUTO_FILL   0x0002
#define COLOR_BG          0xFFE4
#define COLOR_CARD        0xC4DFB4
#define COLOR_CARD_BORDER 0xD6D6
#define COLOR_PRIMARY     0x001F
#define COLOR_GOLD_WARM   0xF800
#define COLOR_BLUE_COOL   0x001F
#define COLOR_SUN         0xF800
#define COLOR_GRAY_LIGHT  0x0000
#define COLOR_GRAY_MID    0x4208
#define COLOR_GRAY_DARK   0x8410
#define COLOR_WHITE       0xFFFF
#define COLOR_GREEN       0x07E0
#define COLOR_CYAN        0x07FF
#define COLOR_RED         0xF800
#define COLOR_ORANGE_YELLOW 0xFD40
#define COLOR_ORANGE_RED    0xFAA0

#define COLOR_TEMP_OUTDOOR  0xF800
#define COLOR_TEMP_INDOOR   0xF800
#define COLOR_TEMP_HIGH     0xF800
#define COLOR_TEMP_LOW      0x001F

struct DailyForecast;

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();
    void init();
    void showConnecting();
    void showConfigMode();
    void clearScreen();
    void fillBlackScreen();
    
    void drawTextWithBg(const char* text, int x, int y, uint16_t color);
    void drawTextWithBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font);
    void drawTextWithTransparentBg(const char* text, int x, int y, uint16_t color);
    void drawTextWithTransparentBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font);

    // XFont 相关公开接口（20px 全字库中文，替代原 font_small_20 的直接 drawString 用法）
    XFont* getXFont() { return _xFont; }
    int getXFontTextWidth(const char* text);
    // datum: TL_DATUM / TC_DATUM / TR_DATUM（其余按 TL 处理），直接绘屏幕
    void drawTextXFont(const char* text, int x, int y, uint16_t color, int datum = TL_DATUM);

    TFT_eSPI& getTFT() { return tft; }
    
    void fadeOut(int durationMs = 200);
    void fadeIn(int durationMs = 200);
    
    bool loadPNGWithBuffer(String filename);
    
    void drawWeatherIcon(int x, int y, const String& code,
                         const char* pathPrefix = "/icon_",
                         const char* fallback = "/icon_999.png");

    static const uint16_t* getBackgroundByIndex(int index) {
        switch (index) {
            case 1: return bg4;
            case 2: return bg6;
            case 3: return bg8;
            default: return bg4;
        }
    }

private:
    TFT_eSPI tft;
    PNG png;
    XFont* _xFont = nullptr;
    int lastBgDay = -1;

    void drawTextWithBgMode(const char* text, int x, int y, uint16_t color,
                             const uint8_t* font, uint16_t bgColor);
    // 20px xfont 画布合成版（透明取 bgSource / auto-fill 取 readRect / 纯色）
    void drawTextXFontBg(const char* text, int x, int y, uint16_t color, uint16_t bgColor);
};

#endif
