#include "TempPage.h"
#include "DisplayManager.h"
#include "WeatherManager.h"
#include "AHT20BMP280Sensor.h"
#include "WiFiManager.h"
#include <esp_log.h>

static const char* TAG = "TempPage";

TempPage::TempPage(DisplayManager& disp, WeatherManager& weather,
                   AHT20BMP280Sensor& aht20, WiFiManager& wifi)
    : _display(disp), _weather(weather), _aht20(aht20), _wifi(wifi) {
    weekdays[0] = "周日";
    weekdays[1] = "周一";
    weekdays[2] = "周二";
    weekdays[3] = "周三";
    weekdays[4] = "周四";
    weekdays[5] = "周五";
    weekdays[6] = "周六";
}

void TempPage::onEnter() {
    ESP_LOGI(TAG, "onEnter");
    _display.clearScreen();
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
    lastForecastTempMin = "";
    lastForecastTempMax = "";
    lastForecastWeatherCode = "";
    lastSunrise = "";
    lastSunset = "";
    lastMoonPhaseIcon = "";
    lastIndoorHumidity = -1000.0f;
    lastIndoorHumidityValid = false;
    lastRSSI = -1000;
    lastApparentTemp = -1000.0f;
    lastApparentHumidity = -1000.0f;
}

void TempPage::updateTime(int year, int month, int day, int hour, int minute, int second, int weekday) {
    drawTime(year, month, day, hour, minute, second, weekday);
}

void TempPage::update() {
    bool forecastValid = !_weather.getForecast(0).date.isEmpty();
    drawWeather(_weather.getCity(), _weather.getWeatherText(), _weather.getTemperature(), _weather.getWeatherCode(), forecastValid);

    const DailyForecast& todayForecast = _weather.getForecast(0);
    drawForecast(todayForecast.tempMin, todayForecast.tempMax, todayForecast.textDay.length() > 0 ? _weather.getWeatherCode() : "");

    drawSunMoon(todayForecast.sunrise, todayForecast.sunset, todayForecast.moonPhaseIcon);

    if (forecastValid && _weather.getTemperature().length() > 0) {
        float temp = _weather.getTemperature().toFloat();
        float humi = todayForecast.humidity.length() > 0 ? todayForecast.humidity.toFloat() : _aht20.getHumidity();
        float apparent = calcApparentTemperature(temp, humi);
        drawApparentTemp(apparent, humi);
    }

    drawIndoorTemp(_aht20.getTemperature(), _aht20.isValid());
    drawIndoorHumidity(_aht20.getHumidity(), _aht20.isValid());
    drawWiFiStatus(_wifi.isConnected());
}

void TempPage::drawTime(int year, int month, int day, int hour, int minute, int second, int weekday) {
    if (lastHour != hour || lastMinute != minute) {
        lastHour = hour;
        lastMinute = minute;
        
        char timeStr[10];
        sprintf(timeStr, "%02d:%02d", hour, minute);
        _display.drawTextWithTransparentBgFont(timeStr, 0, 40, COLOR_WHITE, font_large_72);
    }
    
    if (lastSecond != second) {
        lastSecond = second;
        
        char secStr[5];
        sprintf(secStr, "%02d", second);
        _display.drawTextWithTransparentBgFont(secStr, 78, 28, COLOR_WHITE, font_small_20);
    }
    
    if (lastYear != year || lastMonth != month || lastDay != day) {
        lastYear = year;
        lastMonth = month;
        lastDay = day;
        lastWeekday = weekday;

        char dateStr[20];
        sprintf(dateStr, "%04d.%02d.%02d %s", year, month, day, weekdays[weekday]);
        ESP_LOGI(TAG, "DATE draw at 0,150 = %s", dateStr);
        _display.drawTextWithTransparentBgFont(dateStr, 0, 106, COLOR_WHITE, font_small_20);
    }
}

void TempPage::drawWeather(const String& city, const String& weather, const String& temp, const String& weatherCode, bool forecastValid) {
    if (lastCity != city) {
        ESP_LOGI(TAG, "城市变更: %s", city.c_str());
        lastCity = city;
        
        String cityStr = city.length() > 0 ? city : "--";
        _display.drawTextWithTransparentBg(cityStr.c_str(), 0, 10, COLOR_WHITE);
    }
    
    if (lastWeather != weather || lastForecastValid != forecastValid) {
        ESP_LOGI(TAG, "天气文字变更: %s | 预报有效: %s", weather.c_str(), forecastValid ? "是" : "否");
        lastWeather = weather;
        lastForecastValid = forecastValid;
        
        String weatherStr = weather.length() > 0 ? weather : "--";
        _display.drawTextWithTransparentBg(weatherStr.c_str(), 265, 60, COLOR_WHITE);
        
        TFT_eSPI& tft = _display.getTFT();
        tft.loadFont(font_small_20);
        int weatherWidth = tft.textWidth(weatherStr);
        int weatherHeight = tft.fontHeight();
        tft.unloadFont();
        
        int circleX = 265;  // + weatherWidth + 6;
        int circleY = 40;  // + weatherHeight / 2;
        uint16_t circleColor = forecastValid ? COLOR_GREEN : COLOR_GOLD_WARM;
        tft.fillCircle(circleX, circleY, 5, circleColor);
    }
    
    if (lastWeatherCode != weatherCode) {
        ESP_LOGI(TAG, "天气代码变更: %s", weatherCode.c_str());
        lastWeatherCode = weatherCode;
        _display.drawWeatherIcon(200, 20, weatherCode);
    }
    
    if (lastTemp != temp) {
        ESP_LOGI(TAG, "温度变更: %s", temp.c_str());
        lastTemp = temp;
        
        String tempStr = "外:" + (temp.length() > 0 ? temp : "--");
        _display.drawTextWithTransparentBg(tempStr.c_str(), 150, 130, COLOR_WHITE);
    }
}

void TempPage::drawForecast(const String& tempMin, const String& tempMax, const String& weatherCode) {
    if (lastForecastTempMin == tempMin && lastForecastTempMax == tempMax && lastForecastWeatherCode == weatherCode) {
        return;
    }
    lastForecastTempMin = tempMin;
    lastForecastTempMax = tempMax;
    lastForecastWeatherCode = weatherCode;
    
    float minVal = tempMin.toFloat();
    float maxVal = tempMax.toFloat();
    
    char minStr[10], maxStr[10];
    dtostrf(minVal, 0, 0, minStr);  // 宽度自动，保留1位小数
    dtostrf(maxVal, 0, 0, maxStr);
    
    String forecastStr = String(minStr) + "° - " + String(maxStr) + "°";
    
    _display.drawTextWithTransparentBgFont(forecastStr.c_str(), 190, 95, COLOR_WHITE, font_medium_32);
}

void TempPage::drawSunMoon(const String& sunrise, const String& sunset, const String& moonPhaseIcon) {
    if (lastSunrise == sunrise && lastSunset == sunset && lastMoonPhaseIcon == moonPhaseIcon) {
        return;
    }
    lastSunrise = sunrise;
    lastSunset = sunset;
    lastMoonPhaseIcon = moonPhaseIcon;

    String sunStr = sunrise.length() > 0 ? ("日出:" + sunrise) : "日出--";
    _display.drawTextWithTransparentBg(sunStr.c_str(), 0, 130, COLOR_WHITE);

    String moonStr = sunset.length() > 0 ? ("日落:" + sunset) : "日落--";
    _display.drawTextWithTransparentBg(moonStr.c_str(), 0, 150, COLOR_WHITE);

    // 月相图标：日出日落右边，与"日出"行垂直居中
    if (moonPhaseIcon.length() > 0) {
        // 复用 drawWeatherIcon 全部默认参数：/icon_<code>.png，失败回退 /icon_999.png
        // 32×32 图标（与 weather icon 64×64 区分），位置 (95, 100)
        // 实际区域 95-127 / 100-132，不与 (0,105) 日出文字、(0,126) 日落文字、(225,126) 体感温度冲突
        _display.drawWeatherIcon(97, 122, moonPhaseIcon);
    }
}

void TempPage::drawIndoorTemp(float temp, bool valid) {
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
        _display.drawTextWithTransparentBg(tempStr, 150, 150, COLOR_WHITE);
    } else {
        _display.drawTextWithTransparentBg("内:--", 150, 150, COLOR_WHITE);
    }
}

void TempPage::drawIndoorHumidity(float humidity, bool valid) {
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
        _display.drawTextWithTransparentBg(humStr.c_str(), 225, 150, COLOR_WHITE);
    } else {
        _display.drawTextWithTransparentBg("湿:--%", 225, 150, COLOR_WHITE);
    }
}

void TempPage::drawWiFiStatus(bool connected) {
    int rssi = WiFi.RSSI();
    const int RSSI_HYSTERESIS = 2;
    bool changed = (lastWiFiConnected != connected);
    if (!changed && connected) {
        changed = (abs(rssi - (int)lastRSSI) >= RSSI_HYSTERESIS);
    }
    if (changed) {
        lastWiFiConnected = connected;
        lastRSSI = rssi;

        if (!connected) {
            _display.drawTextWithTransparentBgFont("--", 280, 3, COLOR_GRAY_DARK, font_small_20);
        } else {
            String rssiStr = String(rssi);
            char rssiChar[8];
            rssiStr.toCharArray(rssiChar, sizeof(rssiChar));

            uint16_t wifiColor;
            if (rssi >= -55) {
                wifiColor = COLOR_GREEN;
            } else if (rssi >= -65) {
                wifiColor = COLOR_CYAN;
            } else if (rssi >= -75) {
                wifiColor = COLOR_ORANGE_YELLOW;
            } else if (rssi >= -85) {
                wifiColor = COLOR_ORANGE_RED;
            } else {
                wifiColor = COLOR_RED;
            }

            _display.drawTextWithTransparentBgFont(rssiChar, 285, 3, wifiColor, font_small_20);
        }
    }
}

float TempPage::calcApparentTemperature(float temp, float humidity) {
    if (temp < 10) return temp;
    
    float e = humidity / 100.0 * 6.105 * exp(17.27 * temp / (237.7 + temp));
    float apparent = temp + 0.33 * e - 0.70 * 0 - 4.0;
    return apparent;
}

void TempPage::drawApparentTemp(float apparentTemp, float humidity) {
    if (lastApparentTemp == apparentTemp && lastApparentHumidity == humidity) {
        return;
    }
    lastApparentTemp = apparentTemp;
    lastApparentHumidity = humidity;
    
    char comfort[16];
    if (apparentTemp >= 30) {
        strcpy(comfort, "热");
    } else if (apparentTemp >= 25) {
        strcpy(comfort, "暖");
    } else if (apparentTemp >= 18 && apparentTemp <= 24 && humidity >= 40 && humidity <= 60) {
        strcpy(comfort, "舒");
    } else if (humidity >= 70) {
        strcpy(comfort, "湿");
    } else if (humidity <= 30) {
        strcpy(comfort, "干");
    } else if (apparentTemp <= 10) {
        strcpy(comfort, "冷");
    } else {
        strcpy(comfort, "-");
    }
    
    char tempStr[32];
    sprintf(tempStr, "体:%.1f°%s", apparentTemp, comfort);
    _display.drawTextWithTransparentBg(tempStr, 225, 130, COLOR_WHITE);
}
