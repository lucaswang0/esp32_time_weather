#include "ForecastPage.h"
#include "DisplayManager.h"
#include "WeatherManager.h"

ForecastPage::ForecastPage(DisplayManager& disp, WeatherManager& weather)
    : display(disp), weather(weather) {}

void ForecastPage::onEnter() {
    Serial.println("[ForecastPage] onEnter");
    display.clearScreen();
    display.resetCache();
}

void ForecastPage::update() {
    display.display3DayForecast(
        weather.getForecast(0),
        weather.getForecast(1),
        weather.getForecast(2)
    );
}
