#ifndef FORECAST_PAGE_H
#define FORECAST_PAGE_H

#include "PageBase.h"

class DisplayManager;
class WeatherManager;
class TimeManager;
class WiFiManager;
struct DailyForecast;

class ForecastPage : public PageBase {
public:
    ForecastPage(DisplayManager& disp, WeatherManager& weather,
                 TimeManager& time, WiFiManager& wifi);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& _display;
    WeatherManager& _weather;
    TimeManager& _time;
    WiFiManager& _wifi;

    String lastForecastPageKey = "";

    void draw3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2);
    void drawHeader();
    void drawCard(int x, int y, int w, int h, const DailyForecast& day);
};

#endif
