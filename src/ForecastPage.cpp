#include "ForecastPage.h"
#include "DisplayManager.h"
#include "WeatherManager.h"

ForecastPage::ForecastPage(DisplayManager& disp, WeatherManager& weather)
    : display(disp), weather(weather) {}

void ForecastPage::onEnter() {
    Serial.println("[ForecastPage] onEnter");
    display.clearScreen();
    lastForecastPageKey = "";
}

void ForecastPage::update() {
    draw3DayForecast(
        weather.getForecast(0),
        weather.getForecast(1),
        weather.getForecast(2)
    );
}

void ForecastPage::draw3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2) {
    String key = day0.date + day0.textDay + day0.tempMin + day0.tempMax +
                 day1.date + day1.textDay + day1.tempMin + day1.tempMax +
                 day2.date + day2.textDay + day2.tempMin + day2.tempMax;
    if (key == lastForecastPageKey) {
        return;
    }
    lastForecastPageKey = key;

    const DailyForecast days[3] = {day0, day1, day2};
    const char* labels[3] = {"今天", "明天", "后天"};

    TFT_eSPI& tft = display.getTFT();
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
