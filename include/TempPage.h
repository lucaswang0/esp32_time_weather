#ifndef TEMP_PAGE_H
#define TEMP_PAGE_H

#include "PageBase.h"

class DisplayManager;
class WeatherManager;
class AHT20BMP280Sensor;
class WiFiManager;

class TempPage : public PageBase {
public:
    TempPage(DisplayManager& disp,
             WeatherManager& weather,
             AHT20BMP280Sensor& aht20,
             WiFiManager& wifi);

    void onEnter() override;
    void onExit() override {}
    void update() override;
    
    void updateTime(int year, int month, int day, int hour, int minute, int second, int weekday);

private:
    DisplayManager& display;
    WeatherManager& weather;
    AHT20BMP280Sensor& aht20;
    WiFiManager& wifi;
    
    int lastHour = -1;
    int lastMinute = -1;
    int lastSecond = -1;
    int lastYear = -1;
    int lastMonth = -1;
    int lastDay = -1;
    int lastWeekday = -1;
    String lastCity = "";
    String lastWeather = "";
    String lastTemp = "";
    String lastWeatherCode = "";
    bool lastForecastValid = false;
    bool lastWiFiConnected = false;
    float lastIndoorTemp = -1000.0f;
    bool lastIndoorValid = false;
    String lastForecastTempMin = "";
    String lastForecastTempMax = "";
    String lastForecastWeatherCode = "";
    String lastSunrise = "";
    String lastSunset = "";
    float lastIndoorHumidity = -1000.0f;
    bool lastIndoorHumidityValid = false;
    int lastRSSI = -1000;
    float lastApparentTemp = -1000.0f;
    float lastApparentHumidity = -1000.0f;
    
    const char* weekdays[7];
    
    void drawTime(int year, int month, int day, int hour, int minute, int second, int weekday);
    void drawWeather(const String& city, const String& weather, const String& temp, const String& weatherCode, bool forecastValid);
    void drawForecast(const String& tempMin, const String& tempMax, const String& weatherCode);
    void drawSunMoon(const String& sunrise, const String& sunset);
    void drawIndoorTemp(float temp, bool valid);
    void drawIndoorHumidity(float humidity, bool valid);
    void drawWiFiStatus(bool connected);
    void drawApparentTemp(float apparentTemp, float humidity);
    float calcApparentTemperature(float temp, float humidity);
};

#endif
   