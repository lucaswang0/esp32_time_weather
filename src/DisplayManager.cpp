// ===== 在 DisplayManager.cpp 最开头添加 =====
// 覆盖TFT_eSPI的PNG缓冲区大小
#ifndef PNG_BUFFER_SIZE
#define PNG_BUFFER_SIZE 4096  // 增加到4KB
#endif

// 限制最大图像宽度，减少内存占用
#ifndef MAX_IMAGE_WIDTH
#define MAX_IMAGE_WIDTH 320
#endif


#include "DisplayManager.h"
#include "WeatherManager.h"
#include <SPIFFS.h>
#include <vector>
#include "AHT20BMP280Sensor.h"
extern AHT20BMP280Sensor aht20Bmp280Sensor;
#include <math.h>

static fs::File pngFile;
static TFT_eSPI* pngTft = nullptr;
static PNG* pngObj = nullptr;
static int pngXpos = 0;
static int pngYpos = 0;
static bool pngIsBackground = false;

void* pngOpen(const char *filename, int32_t *size);
void pngClose(void *handle);
int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length);
int32_t pngSeek(PNGFILE *page, int32_t position);
int pngDraw(PNGDRAW *pDraw);

// 用于内存缓冲的全局变量
static uint8_t* pngMemoryBuffer = nullptr;
static size_t pngMemorySize = 0;

// 当前背景图指针（指向 PROGMEM 中的 bg1/bg2/bg3 数组，由 init/clearScreen 根据日期选择）
static const uint16_t* bgSource = nullptr;
static int currentBgIndex = 0;  // 1/2/3

// 全局指针，供 WeatherManager 调用（保留兼容性，实际已不需要释放缓存）
DisplayManager* g_displayManager = nullptr;


DisplayManager::DisplayManager() : 
    lastHour(-1), lastMinute(-1), lastSecond(-1),
    lastMonth(-1), lastDay(-1), lastWeekday(-1),
    lastWiFiConnected(false), lastRSSI(-1000),
    lastCalendarYear(-1), lastCalendarMonth(-1), lastCalendarDay(-1),
    lastDebugPrintTime(0), lastIndoorTemp(-1000.0f), lastIndoorValid(false),
    lastBgDay(-1) {
    
    g_displayManager = this;
    
    weekdays[0] = "周日";
    weekdays[1] = "周一";
    weekdays[2] = "周二";
    weekdays[3] = "周三";
    weekdays[4] = "周四";
    weekdays[5] = "周五";
    weekdays[6] = "周六";
}

void DisplayManager::init() {
    Serial.println("[Display] 初始化显示屏...");

    tft.init();
    tft.setRotation(1);
    // 短暂延迟以确保显示硬件稳定
    delay(1000);

    // 选择背景图（PROGMEM 中的 bg1/bg2/bg3）
    if (lastYear > 0) {
        int today = (lastYear * 10000) + (lastMonth * 100) + lastDay;
        lastBgDay = today;
        randomSeed(today);
        currentBgIndex = random(1, 10);  // 1, 2, ..., 9
        Serial.printf("[Display] Time synced, selecting background for day %d: bg%d\n", today, currentBgIndex);
    } else {
        currentBgIndex = 1;  // 默认
        Serial.println("[Display] Time not synced yet, using temporary background: bg1");
    }
    bgSource = getBackgroundByIndex(currentBgIndex);

    // ESP32 SPI 控制器发送 uint32_t 寄存器时是 LSB-first（bit 0 先发），
    // 所以内存 [B低, R高] 会被发送成 [B低, R高] = 反色。
    // bg*.h 用 FileToCArray 的 Little-Endian 模式生成（uint16_t 原值 0xRRBB），
    // C 编译器按 ESP32 小端存内存为 [B低, R高]，因此 memcpy 时需逐像素交换为 [R高, B低]。
    tft.setSwapBytes(false);

    // 逐行从 PROGMEM 复制到栈缓冲再 pushImage（ESP32-C3 的 DMA 不能直接访问 mmap flash 区间）
    uint16_t lineBuf[SCREEN_WIDTH];
    tft.startWrite();
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        const uint16_t* src = &bgSource[row * SCREEN_WIDTH];
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            lineBuf[col] = __builtin_bswap16(src[col]);  // 字节序交换
        }
        tft.pushImage(0, row, SCREEN_WIDTH, 1, lineBuf);
    }
    tft.endWrite();
    Serial.println("[Display] Background image loaded successfully (from PROGMEM)");
    
    tft.setTextDatum(TL_DATUM);
    // 城市（白色，叠加在 PNG 背景图上）
    drawTextWithTransparentBgFont("0000", 0, 10, COLOR_WHITE, font_small_20);  // 城市

    // 时间显示
    drawTextWithTransparentBgFont("00:00", 0, 40, COLOR_WHITE, font_large_72);  // 时间
    drawTextWithTransparentBgFont("00", 78, 28, COLOR_WHITE, font_small_20);  // 秒

    // 日出/日落
    drawTextWithTransparentBgFont("日出:00:00", 0, 110, COLOR_WHITE, font_small_20);  // 日出
    drawTextWithTransparentBgFont("日落:00:00", 0, 131, COLOR_WHITE, font_small_20);  // 日落

    // 日期
    drawTextWithTransparentBgFont("0000年00月00日 周X", 0, 150, COLOR_WHITE, font_small_20);  // 日期

    // WiFi RSSI
    drawTextWithTransparentBgFont("-00", 285, 3, COLOR_WHITE, font_small_20);  // WiFi RSSI

    // 天气
    drawTextWithTransparentBgFont("0000", 270, 40, COLOR_WHITE, font_small_20);  // 天气文字

    // 预报温度
    drawTextWithTransparentBgFont("00° - 00°", 200, 95, COLOR_WHITE, font_medium_32);  // 预报温度

    // 室外温度
    drawTextWithTransparentBgFont("外:--°", 150, 126, COLOR_WHITE, font_small_20);  // 室外温度

    // 室内温度
    drawTextWithTransparentBgFont("内:--°", 150, 150, COLOR_WHITE, font_small_20);  // 室内温度

    // 体感
    drawTextWithTransparentBgFont("体:--°", 225, 126, COLOR_WHITE, font_small_20);  // 体感

    // AHT20湿度
    drawTextWithTransparentBgFont("湿:--%", 225, 150, COLOR_WHITE, font_small_20);
 
    Serial.println("[Display] 显示屏初始化完成");
}




void DisplayManager::clearScreen() {
    Serial.printf("[Display] 栈高水位: %d\n", uxTaskGetStackHighWaterMark(NULL));

    // 选择背景图（PROGMEM 中的 bg1/bg2/bg3）；每天换一张
    int today = (lastYear * 10000) + (lastMonth * 100) + lastDay;
    if (today != lastBgDay || bgSource == nullptr) {
        lastBgDay = today;
        randomSeed(today);
        currentBgIndex = random(1, 10);  // 1, 2, ..., 9
        bgSource = getBackgroundByIndex(currentBgIndex);
        Serial.printf("[Display] Day changed (%d), selecting new background: bg%d\n", today, currentBgIndex);
    }

    // 字节序：见 init() 注释
    tft.setSwapBytes(false);

    // 逐行从 PROGMEM 复制到缓冲再 pushImage（ESP32-C3 的 DMA 不能直接访问 mmap flash 区间）
    // 用 static 而非栈上分配：避免在 clearScreen 栈帧里占用 640B（与 PReven 越 stack guard 太近）
    static uint16_t lineBuf[SCREEN_WIDTH];
    tft.startWrite();
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        const uint16_t* src = &bgSource[row * SCREEN_WIDTH];
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            lineBuf[col] = __builtin_bswap16(src[col]);  // 字节序交换
        }
        tft.pushImage(0, row, SCREEN_WIDTH, 1, lineBuf);
    }
    tft.endWrite();
}


void DisplayManager::fillBlackScreen() {
    // 实际填白色背景（"fillBlackScreen" 名字保留仅为兼容旧调用点）
    tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_WHITE);
}


void DisplayManager::resetCache() {
    lastHour = -1;
    lastMinute = -1;
    lastSecond = -1;
    lastYear = -1;
    lastMonth = -1;
    lastDay = -1;
    lastWeekday = -1;
    lastCity = "";
    lastWeather = "";
    lastTemp = "";
    lastWeatherCode = "";
    lastForecastValid = false;
    lastWiFiConnected = false;
    lastIndoorTemp = -1000.0f;
    lastIndoorValid = false;
    lastApparentTemp = -1000.0f;
    lastApparentHumidity = -1000.0f;
    lastForecastTempMin = "";
    lastForecastTempMax = "";
    lastForecastWeatherCode = "";
    lastSunrise = "";
    lastSunset = "";
    lastIndoorHumidity = -1000.0f;
    lastIndoorHumidityValid = false;
    lastRSSI = -1000;
    lastCalendarYear = -1;
    lastCalendarMonth = -1;
    lastCalendarDay = -1;
    lastForecastPageKey = "";
    lastBgDay = -1;
}


void DisplayManager::displayTime(int year, int month, int day, int hour, int minute, int second, int weekday) {
    if (lastHour != hour || lastMinute != minute) {
        lastHour = hour;
        lastMinute = minute;
        
        char timeStr[10];
        sprintf(timeStr, "%02d:%02d", hour, minute);
        drawTextWithTransparentBgFont(timeStr, 0, 40, COLOR_WHITE, font_large_72);
    }
    
    if (lastSecond != second) {
        lastSecond = second;
        
        char secStr[5];
        sprintf(secStr, "%02d", second);
        drawTextWithTransparentBgFont(secStr, 78, 28, COLOR_WHITE, font_small_20);
    }
    
    if (lastYear != year || lastMonth != month || lastDay != day) {
        lastYear = year;
        lastMonth = month;
        lastDay = day;
        lastWeekday = weekday;

        char dateStr[20];
        sprintf(dateStr, "%04d.%02d.%02d %s", year, month, day, weekdays[weekday]);
        Serial.printf("[Display] DATE draw at 0,150 = %s\n", dateStr);
        drawTextWithTransparentBgFont(dateStr, 0, 150, COLOR_WHITE, font_small_20);
    }
}


void DisplayManager::displayCalendar(int year, int month, int day) {
    if (year == lastCalendarYear && month == lastCalendarMonth && day == lastCalendarDay) {
        return;
    }
    lastCalendarYear = year;
    lastCalendarMonth = month;
    lastCalendarDay = day;

    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
        daysInMonth[1] = 29;
    }
    
    int firstDayOfWeek = 0;
    {
        int y = year;
        int m = month;
        if (m < 3) {
            m += 12;
            y--;
        }
        firstDayOfWeek = (y + y/4 - y/100 + y/400 + (13*m + 8)/5 + 1) % 7;
    }
    
    int numDays = daysInMonth[month - 1];

    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.loadFont(font_small_20);
    char title[20];
    sprintf(title, "%d年%d月", year, month);
    tft.drawString(title, 160, 5);
    tft.unloadFont();

    tft.loadFont(font_small_20);
    const char* weekDays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int i = 0; i < 7; i++) {
        tft.setTextColor((i == 0 || i == 6) ? COLOR_GOLD_WARM : TFT_WHITE);
        tft.drawString(weekDays[i], 20 + i * 45, 25);
    }

    int dayX = 20;
    int dayY = 45;
    int dayIndex = firstDayOfWeek;

    for (int i = 0; i < firstDayOfWeek; i++) {
        dayX += 45;
    }

    for (int d = 1; d <= numDays; d++) {
        tft.setTextDatum(TC_DATUM);
        // 周末判断（先取模，再用于当天文字颜色）
        bool isWeekend = (dayIndex == 0 || dayIndex == 6);
        if (d == day) {
            tft.fillCircle(dayX, dayY + 9, 14, COLOR_PRIMARY);
            tft.setTextColor(TFT_WHITE);
        } else if (isWeekend) {
            tft.setTextColor(COLOR_GOLD_WARM);
        } else {
            tft.setTextColor(TFT_WHITE);
        }

        char dayStr[4];
        sprintf(dayStr, "%d", d);
        tft.drawString(dayStr, dayX, dayY);

        dayX += 45;
        dayIndex++;

        if (dayIndex >= 7) {
            dayIndex = 0;
            dayX = 20;
            dayY += 27;
        }
    }

    tft.unloadFont();
}

void DisplayManager::displayWeather(const String& city, const String& weather, const String& temp, const String& weatherCode, bool forecastValid) {
    if (lastCity != city) {
        Serial.printf("[Display] 城市变更: %s\n", city.c_str());
        lastCity = city;
        
        String cityStr = city.length() > 0 ? city : "--";
        drawTextWithTransparentBg(cityStr.c_str(), 0, 10, COLOR_WHITE);  // 城市白色
    }
    
    if (lastWeather != weather || lastForecastValid != forecastValid) {
        Serial.printf("[Display] 天气文字变更: %s | 预报有效: %s\n", weather.c_str(), forecastValid ? "是" : "否");
        lastWeather = weather;
        lastForecastValid = forecastValid;
        
        String weatherStr = weather.length() > 0 ? weather : "--";
        drawTextWithTransparentBg(weatherStr.c_str(), 270, 40, COLOR_WHITE);
        
        tft.loadFont(font_small_20);
        int weatherWidth = tft.textWidth(weatherStr);
        int weatherHeight = tft.fontHeight();
        tft.unloadFont();
        
        int circleX = 270 + weatherWidth + 6;
        int circleY = 40 + weatherHeight / 2;
        uint16_t circleColor = forecastValid ? COLOR_GREEN : COLOR_GOLD_WARM;
        tft.fillCircle(circleX, circleY, 5, circleColor);
    }
    
    if (lastWeatherCode != weatherCode) {
        Serial.printf("[Display] 天气代码变更: %s\n", weatherCode.c_str());
        lastWeatherCode = weatherCode;
        drawWeatherIcon(200, 20, weatherCode);
    }
    
    if (lastTemp != temp) {
        Serial.printf("[Display] 温度变更: %s\n", temp.c_str());
        lastTemp = temp;
        
        String tempStr = "外:" + (temp.length() > 0 ? temp : "--");
        drawTextWithTransparentBg(tempStr.c_str(), 150, 126, COLOR_WHITE);
    }
}


void DisplayManager::displayForecast(const String& tempMin, const String& tempMax, const String& weatherCode) {
    if (lastForecastTempMin == tempMin && lastForecastTempMax == tempMax && lastForecastWeatherCode == weatherCode) {
        return;
    }
    lastForecastTempMin = tempMin;
    lastForecastTempMax = tempMax;
    lastForecastWeatherCode = weatherCode;
    
    String forecastStrMin = tempMin + "°"+"- ";
    String forecastStrMax = tempMax + "°";

    String forecastStr = forecastStrMin + forecastStrMax;
    drawTextWithTransparentBgFont(forecastStr.c_str(), 200, 95, COLOR_WHITE, font_medium_32);
}


void DisplayManager::display3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2) {
    String key = day0.date + day0.textDay + day0.tempMin + day0.tempMax +
                 day1.date + day1.textDay + day1.tempMin + day1.tempMax +
                 day2.date + day2.textDay + day2.tempMin + day2.tempMax;
    if (key == lastForecastPageKey) {
        return;
    }
    lastForecastPageKey = key;

    const DailyForecast days[3] = {day0, day1, day2};
    const char* labels[3] = {"今天", "明天", "后天"};

    tft.setTextDatum(TC_DATUM);
    tft.loadFont(font_small_20);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("3天天气预报", 160, 5);

    for (int i = 0; i < 3; i++) {
        int y = 35 + i * 43;
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(labels[i], 8, y);

        String dateText = days[i].date.length() >= 10 ? days[i].date.substring(5) : "--";
        tft.setTextColor(TFT_WHITE);
        tft.drawString(dateText, 58, y);

        String weatherText = days[i].textDay.length() > 0 ? days[i].textDay : "--";
        tft.setTextColor(TFT_WHITE);
        tft.drawString(weatherText, 120, y);

        String tempText = (days[i].tempMin.length() > 0 && days[i].tempMax.length() > 0)
            ? days[i].tempMin + "-" + days[i].tempMax + "°"
            : "--°";
        tft.setTextColor(TFT_WHITE);
        tft.drawString(tempText, 220, y);

        String detailText = "湿" + (days[i].humidity.length() > 0 ? days[i].humidity : "--") + "% " +
                            (days[i].windDir.length() > 0 ? days[i].windDir : "--") +
                            (days[i].windScale.length() > 0 ? days[i].windScale : "--") + "级";
        tft.setTextColor(TFT_WHITE);
        tft.drawString(detailText, 58, y + 20);
    }

    tft.unloadFont();
}


void DisplayManager::displaySunMoon(const String& sunrise, const String& sunset) {
    if (lastSunrise == sunrise && lastSunset == sunset) {
        return;
    }
    lastSunrise = sunrise;
    lastSunset = sunset;

    String sunStr = sunrise.length() > 0 ? ("日出:" + sunrise) : "日出--";
    drawTextWithTransparentBg(sunStr.c_str(), 0, 105, COLOR_WHITE);

    String moonStr = sunset.length() > 0 ? ("日落:" + sunset) : "日落--";
    drawTextWithTransparentBg(moonStr.c_str(), 0, 126, COLOR_WHITE);
}


void DisplayManager::displayIndoorTemp(float temp, bool valid) {
    float prevTemp = lastIndoorTemp;
    bool prevValid = lastIndoorValid;
    
    if (valid) {
        lastIndoorTemp = temp;
    }
    lastIndoorValid = valid;

    if (prevTemp == lastIndoorTemp && prevValid == lastIndoorValid) {
        return;
    }

    if (lastIndoorValid) {
        char tempStr[20];
        sprintf(tempStr, "内:%.1f°", lastIndoorTemp);
        drawTextWithTransparentBg(tempStr, 150, 150, COLOR_WHITE);
    } else {
        drawTextWithTransparentBg("内:--", 150, 150, COLOR_WHITE);
    }
}

// ---- Pressure & Altitude ----

void DisplayManager::displayIndoorHumidity(float humidity, bool valid) {
    float prevHumidity = lastIndoorHumidity;
    bool prevValid = lastIndoorHumidityValid;
    
    if (valid) {
        lastIndoorHumidity = humidity;
    }
    lastIndoorHumidityValid = valid;

    if (prevHumidity == lastIndoorHumidity && prevValid == lastIndoorHumidityValid) {
        return;
    }

    if (lastIndoorHumidityValid) {
        String humStr = "湿:" + String(lastIndoorHumidity, 1) + "%";
        drawTextWithTransparentBg(humStr.c_str(), 225, 150, COLOR_WHITE);
    } else {
        drawTextWithTransparentBg("湿:--%", 225, 150, COLOR_WHITE);
    }
}


void DisplayManager::displayPressure(float pressure, bool valid) {
    static float lastPressure = -1000.0f;
    static bool  lastValid   = false;
    if (valid) {
        if (lastPressure == pressure && lastValid) return;
        lastPressure = pressure;
    }
    lastValid = valid;
    if (valid) {
        char buf[24];
        sprintf(buf, "气压:%.1f hPa", lastPressure);
        // 使用蓝色显示
        drawTextWithTransparentBg(buf, 150, 138, COLOR_WHITE);
    } else {
        drawTextWithTransparentBg("气压:--", 150, 138, COLOR_WHITE);
    }
}


void DisplayManager::displayAltitude(float altitude, bool valid) {
    static float lastAltitude = -1000.0f;
    static bool  lastValid    = false;
    if (valid) {
        if (lastAltitude == altitude && lastValid) return;
        lastAltitude = altitude;
    }
    lastValid = valid;
    if (valid) {
        char buf[24];
        sprintf(buf, "海拔:%.1f m", lastAltitude);
        // 使用绿色显示
        drawTextWithTransparentBg(buf, 150, 156, COLOR_WHITE);
    } else {
        drawTextWithTransparentBg("海拔:--", 150, 156, COLOR_WHITE);
    }
}


float DisplayManager::calcApparentTemperature(float temp, float humidity) {
    if (temp < 10) return temp;
    
    float e = humidity / 100.0 * 6.105 * exp(17.27 * temp / (237.7 + temp));
    float apparent = temp + 0.33 * e - 0.70 * 0 - 4.0;
    return apparent;
}


void DisplayManager::displayApparentTemp(float apparentTemp, float humidity) {
    if (lastApparentTemp == apparentTemp && lastApparentHumidity == humidity) {
        return;
    }
    lastApparentTemp = apparentTemp;
    lastApparentHumidity = humidity;
    
    char comfort[16];
    if (apparentTemp >= 30) {
        strcpy(comfort, "热");   //🥵
    } else if (apparentTemp >= 25) {
        strcpy(comfort, "暖");  //☀
    } else if (apparentTemp >= 18 && apparentTemp <= 24 && humidity >= 40 && humidity <= 60) {
        strcpy(comfort, "舒"); //😊
    } else if (humidity >= 70) {
        strcpy(comfort, "湿");  //💧
    } else if (humidity <= 30) {
        strcpy(comfort, "干");  //👋
    } else if (apparentTemp <= 10) {
        strcpy(comfort, "冷"); //❄
    } else {
        strcpy(comfort, "-");  //😐
    }
    
    char tempStr[32];
    sprintf(tempStr, "体:%.1f°%s", apparentTemp, comfort);
    drawTextWithTransparentBg(tempStr, 225, 126, COLOR_WHITE);
}


void DisplayManager::displayWiFiStatus(bool connected) {
    int rssi = WiFi.RSSI();
    // 迟滞过滤：WiFi.RSSI() 在 1 秒内可抖动 ±2~3 dBm 多次，
    // 用 +1/-2 dBm 的非对称迟滞抑制小幅抖动导致的频繁重绘。
    // 连接状态变化不受迟滞影响（必须立即更新）。
    const int RSSI_HYSTERESIS = 2;  // dBm
    bool changed = (lastWiFiConnected != connected);
    if (!changed && connected) {
        // 已连接状态下，RSSI 变化幅度超过迟滞阈值才算"变化"
        changed = (abs(rssi - (int)lastRSSI) >= RSSI_HYSTERESIS);
    }
    if (changed) {
        lastWiFiConnected = connected;
        lastRSSI = rssi;

        // // 清除之前的内容 - 使用更大的区域确保完全覆盖
        // tft.fillRect(280, 3, 40, 24, COLOR_BG);

        if (!connected) {
            drawTextWithTransparentBgFont("--", 280, 3, COLOR_GRAY_DARK, font_small_20);
        } else {
            String rssiStr = String(rssi);
            char rssiChar[8];
            rssiStr.toCharArray(rssiChar, sizeof(rssiChar));

            // 按信号强度分 5 级显示颜色
            uint16_t wifiColor;
            if (rssi >= -55) {
                wifiColor = COLOR_GREEN;          // 极佳：翠绿 #00FF00
            } else if (rssi >= -65) {
                wifiColor = COLOR_CYAN;           // 良好：亮青 #00FFFF
            } else if (rssi >= -75) {
                wifiColor = COLOR_ORANGE_YELLOW;  // 一般：橙黄 #FFAA00
            } else if (rssi >= -85) {
                wifiColor = COLOR_ORANGE_RED;     // 较差：橙红 #FF5500
            } else {
                wifiColor = COLOR_RED;            // 极差：亮红 #FF0000
            }

            drawTextWithTransparentBgFont(rssiChar, 285, 3, wifiColor, font_small_20);
        }
    }
}


void DisplayManager::showConnecting() {
    tft.fillScreen(COLOR_GRAY_LIGHT);
    // 卡片背景
    tft.fillRoundRect(20, 60, 280, 50, 6, COLOR_CARD);
    tft.loadFont(font_medium_32);
    tft.setTextColor(COLOR_GRAY_LIGHT);
    tft.setCursor(50, 80);
    tft.print("正在连接WiFi...");
    tft.unloadFont();
}


void DisplayManager::showConfigMode() {
    tft.fillScreen(COLOR_GRAY_LIGHT);
    // 卡片背景
    tft.fillRoundRect(30, 20, 260, 110, 6, COLOR_CARD);
    tft.loadFont(font_small_20);
    tft.setTextColor(COLOR_PRIMARY);

    tft.setCursor(50, 30);
    tft.print("WiFi Config Mode");

    tft.setCursor(50, 60);
    tft.setTextColor(COLOR_SUN);
    tft.print("Connect: ESP32-Weather");

    tft.setCursor(50, 85);
    tft.setTextColor(COLOR_GRAY_LIGHT);
    tft.print("pwd: 12345678");

    tft.setCursor(50, 110);
    tft.setTextColor(COLOR_GRAY_MID);
    tft.print("Browser: 192.168.4.1");

    tft.unloadFont();
}

 void DisplayManager::debugPrintScreenVariables() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastDebugPrintTime >= 10000) {
        lastDebugPrintTime = currentTime;
        
        Serial.println("\n[Display] ========== 屏幕变量调试信息 ==========");
        Serial.printf("[Display] 时间 - 时:%02d 分:%02d 秒:%02d\n", lastHour, lastMinute, lastSecond);
        Serial.printf("[Display] 日期 - 月:%02d 日:%02d 星期:%s\n", lastMonth, lastDay, 
                      (lastWeekday >= 0 && lastWeekday < 7) ? weekdays[lastWeekday] : "未知");
        Serial.printf("[Display] 城市: %s\n", lastCity.c_str());
        Serial.printf("[Display] 天气: %s\n", lastWeather.c_str());
        Serial.printf("[Display] 温度: %s\n", lastTemp.c_str());
        Serial.printf("[Display] WiFi连接状态: %s | 信号强度: %ddBm\n", lastWiFiConnected ? "已连接" : "未连接", lastRSSI);
        Serial.printf("[Display] 室内温度: %s\n", lastIndoorValid ? String(lastIndoorTemp, 1) + "°C" : "无效");
        Serial.printf("[Display] 室内湿度: %s\n", lastIndoorHumidityValid ? String(lastIndoorHumidity, 1) + "%" : "无效");
        /* ---------- AHT20 + BMP280 数据 ---------- */
        Serial.printf("[Display] AHT20温度: %.2f°C\n", aht20Bmp280Sensor.getTemperature());
        Serial.printf("[Display] AHT20湿度: %.2f%%\n", aht20Bmp280Sensor.getHumidity());
        Serial.printf("[Display] BMP280气压: %.1f hPa\n", aht20Bmp280Sensor.getPressure());
        float __alt = 44330.0f *
                     (1.0f - powf(aht20Bmp280Sensor.getPressure() / 1013.25f, 0.190284f));
        Serial.printf("[Display] 海拔: %.1f m\n", __alt);
        /* ---------------------------------------- */
        Serial.println("[Display] ==========================================");
        if ( 1 == 2 ){
        tft.loadFont(font_large_72);
        Serial.printf("font_large_72 0(W,H):%d %d\n",tft.textWidth("0"),tft.fontHeight());
        Serial.printf("font_large_72 1(W,H):%d %d\n",tft.textWidth("1"),tft.fontHeight());
        // Serial.printf("font_large_72 温(W,H):%d %d\n",tft.textWidth("温"),tft.fontHeight());
        tft.unloadFont();
        tft.loadFont(font_medium_32);
        Serial.printf("font_medium_32 0(W,H):%d %d\n",tft.textWidth("0"),tft.fontHeight());
        Serial.printf("font_medium_32 1(W,H):%d %d\n",tft.textWidth("1"),tft.fontHeight());
        Serial.printf("font_medium_32 正(W,H):%d %d\n",tft.textWidth("温"),tft.fontHeight());
        tft.unloadFont();
        tft.loadFont(font_small_20);
        Serial.printf("font_small_20 0(W,H):%d %d\n",tft.textWidth("0"),tft.fontHeight());
        Serial.printf("font_small_20 1(W,H):%d %d\n",tft.textWidth("1"),tft.fontHeight());
        Serial.printf("font_small_20 上(W,H):%d %d\n",tft.textWidth("温"),tft.fontHeight());
        tft.unloadFont();
        }
        if ( 1 ==1 ) {
              // 打印各种内存信息
    Serial.println("=== ESP32 内存信息 ===");
    Serial.printf("总堆内存 (Total Heap): %d bytes\n", ESP.getHeapSize());
    Serial.printf("可用堆内存 (Free Heap): %d bytes\n", ESP.getFreeHeap());
    Serial.printf("最小剩余堆内存 (Min Free Heap): %d bytes\n", ESP.getMinFreeHeap());
    Serial.printf("最大可分配连续块: %d bytes\n", ESP.getMaxAllocHeap());
    // 如果你的板子有PSRAM
    if (psramFound()) {
        Serial.printf("总PSRAM: %d bytes\n", ESP.getPsramSize());
        Serial.printf("可用PSRAM: %d bytes\n", ESP.getFreePsram());
    }
    // 打印空闲堆内存，以KB为单位，更直观
    Serial.printf("可用堆内存: %.2f KB\n", ESP.getFreeHeap() / 1024.0);
        }
   }
}


void DisplayManager::fadeOut(int durationMs) {
    int steps = 10;
    int delayMs = durationMs / steps;
    for (int i = 0; i < steps; i++) {
        ledcWrite(BACKLIGHT_CHANNEL, (steps - i) * 255 / steps);
        delay(delayMs);
    }
    ledcWrite(BACKLIGHT_CHANNEL, 0);
}


void DisplayManager::fadeIn(int durationMs) {
    int steps = 10;
    int delayMs = durationMs / steps;
    for (int i = 0; i <= steps; i++) {
        ledcWrite(BACKLIGHT_CHANNEL, i * 255 / steps);
        delay(delayMs);
    }
}


void DisplayManager::drawTextWithBg(const char* text, int x, int y, uint16_t color) {
    drawTextWithBgFont(text, x, y, color, font_small_20);
}


void DisplayManager::drawTextWithBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font) {
    // 屏幕跟随模式：每字符读屏取底色，避免在非白底页面（气压薄荷绿、历史中灰、
    // 日历暖白）文本周围画出白底矩形。代价：每字符一次 readRect + pushSprite。
    drawTextWithBgMode(text, x, y, color, font, COLOR_AUTO_FILL);
}


void DisplayManager::drawTextWithTransparentBg(const char* text, int x, int y, uint16_t color) {
    // 透明背景模式：用于在 PNG 背景图上叠加文字（温度页面等）
    drawTextWithBgMode(text, x, y, color, font_small_20, COLOR_TRANSPARENT);
}


void DisplayManager::drawTextWithTransparentBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font) {
    // 透明背景模式：用于在 PNG 背景图上叠加文字（温度页面等）
    drawTextWithBgMode(text, x, y, color, font, COLOR_TRANSPARENT);
}


void DisplayManager::drawTextWithBgMode(const char* text, int x, int y, uint16_t color,
                                          const uint8_t* font, uint16_t bgColor) {

    TFT_eSprite spr = TFT_eSprite(&tft);

    tft.loadFont(font);
    int w = tft.textWidth(text);
    int h = tft.fontHeight();
    tft.unloadFont();

    if (w <= 0 || h <= 0) return;

    spr.createSprite(w, h);

    int bgX = x;
    int bgY = y;

    if (bgColor == COLOR_TRANSPARENT) {
        // 透明背景模式：从 PROGMEM 中的背景图数组读取背景像素
        if (bgSource != nullptr) {
            // 边界保护：sprite 区域不能超出背景图和屏幕
            int readW = w;
            int readH = h;
            int readX = bgX;
            int readY = bgY;
            // 裁剪到背景图范围内
            if (readX < 0) { readW += readX; readX = 0; }
            if (readY < 0) { readH += readY; readY = 0; }
            if (readX + readW > SCREEN_WIDTH) readW = SCREEN_WIDTH - readX;
            if (readY + readH > SCREEN_HEIGHT) readH = SCREEN_HEIGHT - readY;
            if (readW > 0 && readH > 0) {
                // 先用透明色填充（超出背景图的部分）
                spr.fillSprite(0x0001);
                // 把背景图区域复制到 sprite 对应位置
                // 注：bgSource 是 ESP32 内存字节序（[B低, R高]），
                // sprite pushSprite 发送时按内存字节序（bit 0 先发）= [B低, R高]，
                // 所以要交换为 [R高, B低] 才能正确显示
                for (int row = 0; row < readH; row++) {
                    int dstY = (readY - bgY) + row;
                    int dstX = readX - bgX;
                    if (dstY >= 0 && dstY < h && dstX >= 0 && dstX + readW <= w) {
                        uint16_t* dstPtr = (uint16_t*)spr.getPointer() + dstY * w + dstX;
                        const uint16_t* srcPtr = &bgSource[(readY + row) * SCREEN_WIDTH + readX];
                        for (int col = 0; col < readW; col++) {
                            dstPtr[col] = __builtin_bswap16(srcPtr[col]);
                        }
                    }
                }
            } else {
                spr.fillSprite(0x0001);
            }
        } else {
            // 背景图不可用：使用浅灰兜底，不遮挡背景图
            spr.fillSprite(0x8410);
        }
    } else if (bgColor == COLOR_AUTO_FILL) {
        // 屏幕跟随模式：从当前屏幕像素读取底色（用于薄荷绿/中灰等纯色背景页）
        // 用 tft.readRect() 拉取 (x, y) 开始的 w*h 区域，避免在文本周围产生白底 box
        int readX = bgX;
        int readY = bgY;
        int readW = w;
        int readH = h;
        if (bgX < 0) { readW += bgX; readX = 0; }
        if (bgY < 0) { readH += bgY; readY = 0; }
        if (readX + readW > SCREEN_WIDTH) readW = SCREEN_WIDTH - readX;
        if (readY + readH > SCREEN_HEIGHT) readH = SCREEN_HEIGHT - readY;
        if (readW > 0 && readH > 0) {
            // 字节序：tft.readRect 在 SwapBytes=false 下读到内存布局 [B低, R高]；
            // sprite pushSprite 在同样 SwapBytes=false 下发送 [B低, R高]，布局一致 → 不需要交换
            tft.readRect(readX, readY, readW, readH, (uint16_t*)spr.getPointer());
        } else {
            spr.fillSprite(0x0001);
        }
    } else {
        // 指定背景色模式：直接填充（用于白色背景页面）
        spr.fillSprite(bgColor);
    }

    spr.loadFont(font);
    spr.setTextColor(color);
    spr.setCursor(0, 0);
    spr.print(text);
    spr.unloadFont();

    spr.pushSprite(bgX, bgY);
    spr.deleteSprite();
}


void DisplayManager::drawGradientText(const char* text, int x, int y, uint16_t color1, uint16_t color2) {
    // 1. 先加载字体并测量尺寸
    tft.loadFont(font_small_20);
    
    int textWidth = tft.textWidth(text);
    int textHeight = tft.fontHeight();
    
    // 如果文字宽度或高度为0，直接返回
    if (textWidth <= 0 || textHeight <= 0) {
        tft.unloadFont();
        return;
    }
    
    // 2. 创建离屏精灵（增加边距防止裁剪）
    TFT_eSprite spr = TFT_eSprite(&tft);
    int padding = 4;
    spr.createSprite(textWidth + padding * 2, textHeight + padding * 2);
    spr.fillSprite(COLOR_GRAY_LIGHT);
    
    // 3. 用白色绘制文字到精灵
    spr.setTextColor(TFT_WHITE);
    spr.setTextDatum(TL_DATUM);
    spr.setCursor(padding, padding);
    spr.print(text);
    
    // 4. 获取像素缓冲区（需要强制类型转换）
    uint16_t* buffer = (uint16_t*)spr.getPointer();  // ← 关键修改：添加 (uint16_t*) 强制转换
    if (buffer == nullptr) {
        spr.deleteSprite();
        tft.unloadFont();
        return;
    }
    
    int w = spr.width();
    int h = spr.height();
    
    // 5. 预计算颜色分量（提高性能）
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    // 6. 逐像素处理渐变
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            uint16_t pixel = buffer[py * w + px];
            
            // 只处理白色像素（文字部分）
            if (pixel == TFT_WHITE) {
                // 计算渐变比例
                float ratio = 0.0;
                if (textWidth > 0) {
                    ratio = (float)(px - padding) / textWidth;
                    if (ratio < 0.0) ratio = 0.0;
                    if (ratio > 1.0) ratio = 1.0;
                }
                
                // 混合颜色
                uint8_t r = r1 + (uint8_t)((r2 - r1) * ratio);
                uint8_t g = g1 + (uint8_t)((g2 - g1) * ratio);
                uint8_t b = b1 + (uint8_t)((b2 - b1) * ratio);
                
                buffer[py * w + px] = tft.color565(r, g, b);
            }
        }
    }
    
    // 7. 绘制到屏幕
    spr.pushSprite(x, y);
    
    // 8. 释放资源
    spr.deleteSprite();
    tft.unloadFont();
}


String DisplayManager::getWeatherIconText(int weatherCode) {
    switch(weatherCode) {
        case 100: return "晴";
        case 101: return "多云";
        case 102: return "少云";
        case 103: return "晴间多云";
        case 104: return "阴";
        case 150: return "晴";
        case 151: return "多云";
        case 152: return "少云";
        case 153: return "晴间多云";
        case 300: return "阵雨";
        case 301: return "强阵雨";
        case 302: return "雷阵雨";
        case 303: return "强雷阵雨";
        case 304: return "雷阵雨伴冰雹";
        case 305: return "小雨";
        case 306: return "中雨";
        case 307: return "大雨";
        case 308: return "极端降雨";
        case 309: return "毛毛雨";
        case 310: return "暴雨";
        case 311: return "大暴雨";
        case 312: return "特大暴雨";
        case 313: return "冻雨";
        case 314: return "小到中雨";
        case 315: return "中到大雨";
        case 316: return "大到暴雨";
        case 317: return "暴雨到大暴雨";
        case 318: return "大暴雨到特大暴雨";
        case 350: return "阵雨";
        case 351: return "强阵雨";
        case 399: return "雨";
        case 400: return "小雪";
        case 401: return "中雪";
        case 402: return "大雪";
        case 403: return "暴雪";
        case 404: return "雨夹雪";
        case 405: return "雨雪天气";
        case 406: return "阵雨夹雪";
        case 407: return "阵雪";
        case 408: return "小到中雪";
        case 409: return "中到大雪";
        case 410: return "大到暴雪";
        case 456: return "阵雨夹雪";
        case 457: return "阵雪";
        case 499: return "雪";
        case 500: return "薄雾";
        case 501: return "雾";
        case 502: return "霾";
        case 503: return "扬沙";
        case 504: return "浮尘";
        case 507: return "沙尘暴";
        case 508: return "强沙尘暴";
        case 509: return "浓雾";
        case 510: return "强浓雾";
        case 511: return "中度霾";
        case 512: return "重度霾";
        case 513: return "严重霾";
        case 514: return "大雾";
        case 515: return "特强浓雾";
        case 900: return "热";
        case 901: return "冷";
        case 800: return "新月";
        case 801: return "蛾眉月";
        case 802: return "上弦月";
        case 803: return "盈凸月";
        case 804: return "满月";
        case 805: return "亏凸月";
        case 806: return "下弦月";
        case 807: return "残月";
        case 999: return "未知";
        default: return "未知";
    }
}


#define WEATHER_ICON_WIDTH 64
#define WEATHER_ICON_HEIGHT 64


void* pngOpen(const char *filename, int32_t *size) {
    Serial.printf("[PNG] Opening: %s\n", filename);
    pngFile = SPIFFS.open(filename, FILE_READ);
    if (!pngFile) {
        Serial.printf("[PNG] File open failed: %s\n", filename);
        *size = 0;
        return NULL;
    }
    *size = pngFile.size();
    return &pngFile;
}


void pngClose(void *handle) {
    (void)handle;
    if (pngFile) {
        pngFile.close();
    }
}


int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length) {
    (void)page;
    if (!pngFile) return 0;
    return pngFile.read(buffer, length);
}


int32_t pngSeek(PNGFILE *page, int32_t position) {
    (void)page;
    if (!pngFile) return 0;
    return pngFile.seek(position);
}


int pngDraw(PNGDRAW *pDraw) {
    if (!pngTft || !pngObj) return 0;
    uint16_t lineBuffer[MAX_IMAGE_WIDTH];
    pngObj->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);


    if (pngIsBackground) {
        // 背景图全屏绘制：用 pushImage 一次性写入整行（同步阻塞模式由外层 startWrite/endWrite 包裹）
        int y = pngYpos + pDraw->y;
        int w = pDraw->iWidth;
        if (w > MAX_IMAGE_WIDTH) w = MAX_IMAGE_WIDTH;
        pngTft->pushImage(pngXpos, y, w, 1, lineBuffer);
        // 注：背景图已改为 PROGMEM 数组，不再写入 RAM 缓存
    } else {
        int y = pngYpos + pDraw->y;
        // 用 getAlphaMask 取阈值为 128 的 1bpp alpha mask，
        // 按 8 像素/字节打包，按位解包后只画 a>=128 的像素。
        // lineBuffer 用 0xFFFFFFFF（关闭 alpha 混合）输出纯图标 RGB，
        // 不依赖特定色键，避免"忽略色键->黑色像素覆盖背景"的问题。
        const int maskBytes = (pDraw->iWidth + 7) >> 3;
        uint8_t mask[MAX_IMAGE_WIDTH / 8];
        pngObj->getAlphaMask(pDraw, mask, 128);
        for (int x = 0; x < pDraw->iWidth; x++) {
            uint8_t bit = mask[x >> 3] & (0x80 >> (x & 7));
            if (bit) {
                pngTft->drawPixel(pngXpos + x, y, lineBuffer[x]);
            }
        }
        (void)maskBytes;
    }
    return 1;
}


void DisplayManager::prepareBgCache(const String& filename) {
    // 已废弃：背景图改为 PROGMEM 数组，无需 RAM 缓存
    // 保留空函数以兼容旧调用点（如果存在）
    (void)filename;
}


void DisplayManager::releaseBgCache() {
    // 已废弃：背景图改为 PROGMEM 数组，无 RAM 缓存可释放
}


void DisplayManager::restoreBgCache() {
    // 已废弃：背景图改为 PROGMEM 数组，无需恢复
}


void DisplayManager::drawWeatherIcon(int x, int y, const String& weatherCodeStr) {
    if (weatherCodeStr.isEmpty()) {
        Serial.println("[Display] 天气代码为空");
        return;
    }
    
    int weatherCode = weatherCodeStr.toInt();
    Serial.printf("[Display] 天气代码: %s -> %d\n", weatherCodeStr.c_str(), weatherCode);
    
    String iconFile = "/icon_" + String(weatherCode) + ".png";
    
    pngTft = &tft;
    pngObj = &png;
    pngXpos = x;
    pngYpos = y;
    pngIsBackground = false;
    
    int16_t rc = png.open(iconFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
    if (rc == PNG_SUCCESS) {
        tft.startWrite();
        Serial.printf("[PNG] Image specs: (%d x %d), %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());
        
        if (bgSource != nullptr) {
            tft.setSwapBytes(false);
            static uint16_t lineBuf[SCREEN_WIDTH];
            for (int row = 0; row < png.getHeight(); row++) {
                int bgY = y + row;
                if (bgY >= 0 && bgY < SCREEN_HEIGHT) {
                    int bgStartIndex = bgY * SCREEN_WIDTH + x;
                    for (int col = 0; col < png.getWidth() && x + col < SCREEN_WIDTH; col++) {
                        uint16_t pixel = pgm_read_word(&bgSource[bgStartIndex + col]);
                        lineBuf[col] = (pixel >> 8) | ((pixel & 0xFF) << 8);
                    }
                    tft.pushImage(x, bgY, png.getWidth(), 1, lineBuf);
                }
            }
            tft.setSwapBytes(true);
        }
        
        rc = png.decode(NULL, 0);
        tft.endWrite();
        png.close();
    } else {
        Serial.printf("[PNG] Open failed: %d, trying default icon\n", rc);
        
        String defaultFile = "/icon_100.png";
        rc = png.open(defaultFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
        if (rc == PNG_SUCCESS) {
            tft.startWrite();
            Serial.printf("[PNG] Default icon specs: (%d x %d), %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());
            
            if (bgSource != nullptr) {
                tft.setSwapBytes(false);
                static uint16_t lineBuf[SCREEN_WIDTH];
                for (int row = 0; row < png.getHeight(); row++) {
                    int bgY = y + row;
                    if (bgY >= 0 && bgY < SCREEN_HEIGHT) {
                        int bgStartIndex = bgY * SCREEN_WIDTH + x;
                        for (int col = 0; col < png.getWidth() && x + col < SCREEN_WIDTH; col++) {
                            uint16_t pixel = pgm_read_word(&bgSource[bgStartIndex + col]);
                            lineBuf[col] = (pixel >> 8) | ((pixel & 0xFF) << 8);
                        }
                        tft.pushImage(x, bgY, png.getWidth(), 1, lineBuf);
                    }
                }
                tft.setSwapBytes(true);
            }
            
            rc = png.decode(NULL, 0);
            tft.endWrite();
            png.close();
        } else {
            Serial.println("[PNG] Default icon also failed");
        }
    }
}

// ---------- 背景图片绘制 ----------

void DisplayManager::displayBackground(const String& path) {
    if (path.isEmpty()) {
        Serial.println("[Display] 背景路径为空");
        return;
    }
    pngTft = &tft;
    pngObj = &png;
    pngXpos = 0;
    pngYpos = 0;
    pngIsBackground = true;
    int16_t rc = png.open(path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
    if (rc == PNG_SUCCESS) {
        tft.startWrite();
        rc = png.decode(NULL, 0);
        tft.endWrite();
        png.close();
    } else {
        Serial.printf("[Display] 打开背景图片失败: %d %s\n", rc, path.c_str());
    }
}

// ========== 内存缓冲加载函数 ==========

bool DisplayManager::loadPNGWithBuffer(String filename) {
    Serial.printf("[Display] 尝试内存缓冲加载: %s\n", filename.c_str());
    
    // 检查可用内存
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[Display] 加载前可用内存: %d bytes\n", freeHeap);
    
    // 打开文件
    fs::File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.println("[Display] ❌ 无法打开文件");
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.printf("[Display] 文件大小: %d bytes\n", fileSize);
    
    // 限制文件大小（最大50KB）
    if (fileSize > 50000) {
        Serial.printf("[Display] ❌ 文件太大: %d bytes (限制50KB)\n", fileSize);
        file.close();
        return false;
    }
    
    // 检查内存是否足够（文件大小 + 解码缓冲区）
    if (freeHeap < fileSize + 20000) {
        Serial.printf("[Display] ❌ 内存不足: 需要 %d, 可用 %d\n", fileSize + 20000, freeHeap);
        file.close();
        return false;
    }
    
    // 分配内存读取整个文件
    uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
    if (!fileBuffer) {
        Serial.println("[Display] ❌ 内存分配失败");
        file.close();
        return false;
    }
    
    // 读取文件到内存
    size_t bytesRead = file.read(fileBuffer, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        Serial.printf("[Display] ❌ 读取不完整: %d/%d bytes\n", bytesRead, fileSize);
        free(fileBuffer);
        return false;
    }
    
    Serial.printf("[Display] ✅ 文件读取成功: %d bytes\n", bytesRead);
    
    // 设置PNG解码参数
    pngTft = &tft;
    pngObj = &png;
    pngXpos = 0;
    pngYpos = 0;
    pngIsBackground = true;
    
    // 使用内存打开PNG（关键：使用 openRAM）
    int16_t rc = png.openRAM(fileBuffer, fileSize, pngDraw);
    Serial.printf("[Display] png.openRAM() result: %d\n", rc);
    
    if (rc == PNG_SUCCESS) {
        // 获取PNG信息
        int width = png.getWidth();
        int height = png.getHeight();
        int bpp = png.getBpp();
        Serial.printf("[Display] PNG: %dx%d, %d bpp\n", width, height, bpp);
        
        // 检查尺寸是否合理
        if (width > 320 || height > 170) {
            Serial.printf("[Display] ⚠️ 图片尺寸异常: %dx%d\n", width, height);
            png.close();
            free(fileBuffer);
            return false;
        }
        
        // 解码PNG
        tft.startWrite();
        rc = png.decode(NULL, 0);
        tft.endWrite();
        png.close();
        
        // 释放内存缓冲
        free(fileBuffer);
        
        if (rc == PNG_SUCCESS) {
            Serial.println("[Display] ✅ 内存缓冲解码成功");
            return true;
        } else {
            Serial.printf("[Display] ❌ 内存缓冲解码失败: %d\n", rc);
            return false;
        }
    } else {
        Serial.printf("[Display] ❌ PNG打开失败: %d\n", rc);
        free(fileBuffer);
        return false;
    }
}


void DisplayManager::drawWifiIcon(int x, int y, int size, bool connected, int rssi) {
    uint16_t wifiColor;

    if (!connected) {
        wifiColor = COLOR_GRAY_DARK;
    } else if (rssi >= -55) {
        wifiColor = COLOR_PRIMARY;      // 强信号 - 深蓝
    } else if (rssi >= -70) {
        wifiColor = COLOR_GOLD_WARM;    // 中信号 - 红色
    } else {
        wifiColor = COLOR_SUN;          // 弱信号 - 红色
    }

    tft.fillRect(x, y, size + 45, size, COLOR_GRAY_LIGHT);

    if (!connected) {
        tft.drawLine(x + 6, y + 6, x + size - 7, y + size - 7, wifiColor);
        tft.drawLine(x + size - 7, y + 6, x + 6, y + size - 7, wifiColor);
        return;
    }

    tft.loadFont(font_small_20);
    tft.setTextColor(wifiColor);
    String rssiStr = String(rssi);
    tft.setCursor(x-3, y+5);
    tft.print(rssiStr);
    tft.unloadFont();
}


static float calcAltitudeFromPressure(float pressure) {
    if (pressure <= 0) return 0;
    return 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.190284f));
}

