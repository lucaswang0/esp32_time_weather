#ifndef FORECAST_PAGE_H
#define FORECAST_PAGE_H

#include "PageBase.h"

class DisplayManager;
class WeatherManager;

/**
 * 3 天天气预报页面（今天 / 明天 / 后天）
 */
class ForecastPage : public PageBase {
public:
    ForecastPage(DisplayManager& disp, WeatherManager& weather);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& display;
    WeatherManager& weather;
};

#endif
