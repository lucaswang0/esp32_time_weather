#ifndef FORECAST_PAGE_H
#define FORECAST_PAGE_H

#include "PageBase.h"

class DisplayManager;
class WeatherManager;
struct DailyForecast;

class ForecastPage : public PageBase {
public:
    ForecastPage(DisplayManager& disp, WeatherManager& weather);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& display;
    WeatherManager& weather;
    
    String lastForecastPageKey = "";
    
    void draw3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2);
};

#endif
