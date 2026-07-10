#ifndef TEMP_PAGE_H
#define TEMP_PAGE_H

#include "PageBase.h"

class DisplayManager;
class WeatherManager;
class AHT20BMP280Sensor;
class WiFiManager;

/**
 * 温度页面：时间 / 室外天气 / 预报 / 日出日落 / 室内温湿度 / WiFi RSSI
 */
class  TempPage : public PageBase {
public:
    TempPage(DisplayManager& disp,
             WeatherManager& weather,
             AHT20BMP280Sensor& aht20,
             WiFiManager& wifi);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& display;
    WeatherManager& weather;
    AHT20BMP280Sensor& aht20;
    WiFiManager& wifi;
};

#endif
   