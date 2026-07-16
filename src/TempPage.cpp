#include "TempPage.h"
#include "DisplayManager.h"
#include "WeatherManager.h"
#include "AHT20BMP280Sensor.h"
#include "WiFiManager.h"

TempPage::TempPage(DisplayManager& disp, WeatherManager& weather,
                   AHT20BMP280Sensor& aht20, WiFiManager& wifi)
    : display(disp), weather(weather), aht20(aht20), wifi(wifi) {
    weekdays[0] = "周日";
    weekdays[1] = "周一";
    weekdays[2] = "周二";
    weekdays[3] = "周三";
    weekdays[4] = "周四";
    weekdays[5] = "周五";
    weekdays[6] = "周六";
}

void TempPage::onEnter() {
    Serial.println("[TempPage] onEnter");
    display.clearScreen();
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
    bool forecastValid = !weather.getForecast(0).date.isEmpty();
    drawWeather(weather.getCity(), weather.getWeatherText(), weather.getTemperature(), weather.getWeatherCode(), forecastValid);

    const DailyForecast& todayForecast = weather.getForecast(0);
    drawForecast(todayForecast.tempMin, todayForecast.tempMax, todayForecast.textDay.length() > 0 ? weather.getWeatherCode() : "");

    drawSunMoon(todayForecast.sunrise, todayForecast.sunset);

    if (forecastValid && weather.getTemperature().length() > 0) {
        float temp = weather.getTemperature().toFloat();
        float humi = todayForecast.humidity.length() > 0 ? todayForecast.humidity.toFloat() : aht20.getHumidity();
        float apparent = calcApparentTemperature(temp, humi);
        drawApparentTemp(apparent, humi);
    }

    drawIndoorTemp(aht20.getTemperature(), aht20.isValid());
    drawIndoorHumidity(aht20.getHumidity(), aht20.isValid());
    drawWiFiStatus(wifi.isConnected());
}

void TempPage::drawTime(int year, int month, int day, int hour, int minute, int second, int weekday) {
    if (lastHour != hour || lastMinute != minute) {
        lastHour = hour;
        lastMinute = minute;
        
        char timeStr[10];
        sprintf(timeStr, "%02d:%02d", hour, minute);
        display.drawTextWithTransparentBgFont(timeStr, 0, 40, COLOR_WHITE, font_large_72);
    }
    
    if (lastSecond != second) {
        lastSecond = second;
        
        char secStr[5];
        sprintf(secStr, "%02d", second);
        display.drawTextWithTransparentBgFont(secStr, 78, 28, COLOR_WHITE, font_small_20);
    }
    
    if (lastYear != year || lastMonth != month || lastDay != day) {
        lastYear = year;
        lastMonth = month;
        lastDay = day;
        lastWeekday = weekday;

        char dateStr[20];
        sprintf(dateStr, "%04d.%02d.%02d %s", year, month, day, weekdays[weekday]);
        Serial.printf("[TempPage] DATE draw at 0,150 = %s\n", dateStr);
        display.drawTextWithTransparentBgFont(dateStr, 0, 150, COLOR_WHITE, font_small_20);
    }
}

void TempPage::drawWeather(const String& city, const String& weather, const String& temp, const String& weatherCode, bool forecastValid) {
    if (lastCity != city) {
        Serial.printf("[TempPage] 城市变更: %s\n", city.c_str());
        lastCity = city;
        
        String cityStr = city.length() > 0 ? city : "--";
        display.drawTextWithTransparentBg(cityStr.c_str(), 0, 10, COLOR_WHITE);
    }
    
    if (lastWeather != weather || lastForecastValid != forecastValid) {
        Serial.printf("[TempPage] 天气文字变更: %s | 预报有效: %s\n", weather.c_str(), forecastValid ? "是" : "否");
        lastWeather = weather;
        lastForecastValid = forecastValid;
        
        String weatherStr = weather.length() > 0 ? weather : "--";
        display.drawTextWithTransparentBg(weatherStr.c_str(), 270, 40, COLOR_WHITE);
        
        TFT_eSPI& tft = display.getTFT();
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
        Serial.printf("[TempPage] 天气代码变更: %s\n", weatherCode.c_str());
        lastWeatherCode = weatherCode;
        display.drawWeatherIcon(200, 20, weatherCode);
    }
    
    if (lastTemp != temp) {
        Serial.printf("[TempPage] 温度变更: %s\n", temp.c_str());
        lastTemp = temp;
        
        String tempStr = "外:" + (temp.length() > 0 ? temp : "--");
        display.drawTextWithTransparentBg(tempStr.c_str(), 150, 126, COLOR_WHITE);
    }
}

void TempPage::drawForecast(const String& tempMin, const String& tempMax, const String& weatherCode) {
    if (lastForecastTempMin == tempMin && lastForecastTempMax == tempMax && lastForecastWeatherCode == weatherCode) {
        return;
    }
    lastForecastTempMin = tempMin;
    lastForecastTempMax = tempMax;
    lastForecastWeatherCode = weatherCode;
    
    String forecastStrMin = tempMin + "°" + "- ";
    String forecastStrMax = tempMax + "°";
    String forecastStr = forecastStrMin + forecastStrMax;
    display.drawTextWithTransparentBgFont(forecastStr.c_str(), 200, 95, COLOR_WHITE, font_medium_32);
}

void TempPage::drawSunMoon(const String& sunrise, const String& sunset) {
    if (lastSunrise == sunrise && lastSunset == sunset) {
        return;
    }
    lastSunrise = sunrise;
    lastSunset = sunset;

    String sunStr = sunrise.length() > 0 ? ("日出:" + sunrise) : "日出--";
    display.drawTextWithTransparentBg(sunStr.c_str(), 0, 105, COLOR_WHITE);

    String moonStr = sunset.length() > 0 ? ("日落:" + sunset) : "日落--";
    display.drawTextWithTransparentBg(moonStr.c_str(), 0, 126, COLOR_WHITE);
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
        display.drawTextWithTransparentBg(tempStr, 150, 150, COLOR_WHITE);
    } else {
        display.drawTextWithTransparentBg("内:--", 150, 150, COLOR_WHITE);
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
        display.drawTextWithTransparentBg(humStr.c_str(), 225, 150, COLOR_WHITE);
    } else {
        display.drawTextWithTransparentBg("湿:--%", 225, 150, COLOR_WHITE);
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
            display.drawTextWithTransparentBgFont("--", 280, 3, COLOR_GRAY_DARK, font_small_20);
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

            display.drawTextWithTransparentBgFont(rssiChar, 285, 3, wifiColor, font_small_20);
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
    display.drawTextWithTransparentBg(tempStr, 225, 126, COLOR_WHITE);
}
