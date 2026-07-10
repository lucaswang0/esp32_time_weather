#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include <Arduino.h>
#include <stdlib.h>
#include <WiFi.h>
#include <PNGdec.h>
#include "font_large_72.h"
#include "font_medium_32.h"
#include "font_small_20.h"
#include "config.h"
// #include "weather_icons.h"  // 已改用 SPIFFS 中的 PNG 背景图片
// #include "weather_bg.h"  // 已改用 SPIFFS 中的 PNG 背景图片
// 背景图 PROGMEM 数组（编译期嵌入 flash，零 RAM 占用）
#include "bg1.h"
#include "bg2.h"
#include "bg3.h"
#include "bg4.h"
#include "bg5.h"
#include "bg6.h"
#include "bg7.h"
#include "bg8.h"
#include "bg9.h"

extern const int BACKLIGHT_CHANNEL;

// ==================== 明亮浅色主题配色 ====================
// COLOR_TRANSPARENT 是 drawTextWithBgMode 的特殊哨兵值，触发"读屏幕背景"模式（用于 PNG 背景图上叠加文字）
// COLOR_AUTO_FILL 是 drawTextWithBgMode 的另一种哨兵值，从当前屏幕实际像素读取底色
//   （用于纯色背景页面如薄荷绿/中灰，避免在文本周围画出白底矩形）
#define COLOR_TRANSPARENT  0x0001
#define COLOR_AUTO_FILL   0x0002
#define COLOR_BG          0xFFE4   // 背景: 暖白 #FFF8DC
// #define COLOR_CARD        0xF7DE   // 卡片背景: #F0F0F0
#define COLOR_CARD        0xC4DFB4   // 卡片背景: #C0C0C0
#define COLOR_CARD_BORDER 0xD6D6   // 卡片边框: #D0D0D0
#define COLOR_PRIMARY     0x001F   // 主深蓝: #00008B
#define COLOR_GOLD_WARM   0xF800   // 红色: #FF0000
#define COLOR_BLUE_COOL   0x001F   // 深蓝: #00008B
#define COLOR_SUN         0xF800   // 红色: #FF0000
#define COLOR_GRAY_LIGHT  0x0000   // 黑色: #000000
#define COLOR_GRAY_MID    0x4208   // 深灰: #404040
#define COLOR_GRAY_DARK   0x8410   // 中灰: #808080
#define COLOR_WHITE       0xFFFF   // 白色
#define COLOR_GREEN       0x07E0   // 亮绿: #00FF00
#define COLOR_CYAN        0x07FF   // 亮青: #00FFFF
#define COLOR_RED         0xF800   // 亮红: #FF0000
// 自定义橙色（无 TFT_eSPI 内置）
// RGB565 公式: ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
#define COLOR_ORANGE_YELLOW 0xFD40  // 橙黄 #FFAA00 (R=31,G=21,B=0)
#define COLOR_ORANGE_RED    0xFAA0  // 橙红 #FF5500 (R=31,G=10,B=0)

// ===== 温度相关颜色 =====
#define COLOR_TEMP_OUTDOOR  0xF800  // 红色
#define COLOR_TEMP_INDOOR   0xF800  // 红色
#define COLOR_TEMP_HIGH     0xF800  // 红色
#define COLOR_TEMP_LOW      0x001F  // 深蓝


struct DailyForecast;

class DisplayManager {
public:
    DisplayManager();
    void init();
    void displayTime(int year, int month, int day, int hour, int minute, int second, int weekday);
    void displayWeather(const String& city, const String& weather, const String& temp, const String& weatherCode, bool forecastValid);
    void displayWiFiStatus(bool connected);
    void showConnecting();
    void showConfigMode();
    void clearScreen();
    void fillBlackScreen();
    void resetCache();
    void debugPrintScreenVariables();
    String getWeatherIconText(int weatherCode);
    void displayIndoorTemp(float temp, bool valid);
    void displayPressure(float pressure, bool valid);
    void displayAltitude(float altitude, bool valid);
    void displayForecast(const String& tempMin, const String& tempMax, const String& weatherCode);
    void displaySunMoon(const String& sunrise, const String& sunset);
    void displayIndoorHumidity(float humidity, bool valid);  // 室内湿度显示（AHT20）
    void displayCalendar(int year, int month, int day);
    void display3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2);
    void drawTextWithBg(const char* text, int x, int y, uint16_t color);  // 带背景的文字绘制（使用small字体，白色底）
    void drawTextWithBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font);  // 带背景的文字绘制（指定字体，白色底）
    void drawTextWithTransparentBg(const char* text, int x, int y, uint16_t color);  // 透明背景文字（用于PNG背景图上）
    void drawTextWithTransparentBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font);  // 透明背景文字（指定字体）
    TFT_eSPI& getTFT() { return tft; }
    
    void fadeOut(int durationMs = 200);
    void fadeIn(int durationMs = 200);
    float calcApparentTemperature(float temp, float humidity);
    void displayApparentTemp(float apparentTemp, float humidity);
    // 新增：内存缓冲加载PNG
    bool loadPNGWithBuffer(String filename);
    // 已废弃：背景图改为 PROGMEM 数组后无需 RAM 缓存，保留空方法兼容旧调用
    void prepareBgCache(const String& filename);
    void releaseBgCache();
    void restoreBgCache();

    // 根据索引获取背景图 PROGMEM 数组（1=bg1, 2=bg2, 3=bg3）
    static const uint16_t* getBackgroundByIndex(int index) {
        switch (index) {
            case 1: return bg1;
            case 2: return bg2;
            case 3: return bg3;
            case 4: return bg4;
            case 5: return bg5;
            case 6: return bg6;
            case 7: return bg7;
            case 8: return bg8;
            case 9: return bg9;
            default: return bg1;
        }
    }

private:
    TFT_eSPI tft;
    PNG png;
    
    // 缓存变量（避免不必要的刷新）
    int lastYear;
    int lastHour;
    int lastMinute;
    int lastSecond;
    int lastMonth;
    int lastDay;
    int lastWeekday;
    String lastCity;
    String lastWeather;
    String lastTemp;
    String lastWeatherCode;
    bool lastForecastValid;
    bool lastWiFiConnected;
    float lastIndoorTemp;
    bool lastIndoorValid;
    String lastForecastTempMin;
    String lastForecastTempMax;
    String lastForecastWeatherCode;
    String lastSunrise;
    String lastSunset;
    
    // AHT20 室内温湿度缓存变量
    float lastIndoorHumidity;
    bool lastIndoorHumidityValid;
    
    // WiFi信号强度缓存
    int lastRSSI;
    
    // 体感温度缓存
    float lastApparentTemp;
    float lastApparentHumidity;
    int lastCalendarYear;
    int lastCalendarMonth;
    int lastCalendarDay;
    String lastForecastPageKey;
    
    // 调试变量
    unsigned long lastDebugPrintTime;
    
    // 背景图缓存变量
    int lastBgDay;
    
    const char* weekdays[7];
    
    // WiFi图标绘制函数
    void drawWifiIcon(int x, int y, int size, bool connected, int rssi);

    // 天气图标绘制函数
    void drawWeatherIcon(int x, int y, const String& weatherCode);

    // 背景图绘制函数
    void displayBackground(const String& path);

    // 渐变文字绘制函数
    void drawGradientText(const char* text, int x, int y, uint16_t color1, uint16_t color2);

    // 通用绘制入口：bgColor == COLOR_TRANSPARENT 时读屏幕背景；其它值直接填充该色
    void drawTextWithBgMode(const char* text, int x, int y, uint16_t color,
                             const uint8_t* font, uint16_t bgColor);
};

#endif