#include "ForecastPage.h"
#include <esp_log.h>
#include "DisplayManager.h"
#include "WeatherManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"

static const char* TAG = "ForecastPage";
ForecastPage::ForecastPage(DisplayManager& disp, WeatherManager& weather,
                           TimeManager& time, WiFiManager& wifi)
    : _display(disp), _weather(weather), _time(time), _wifi(wifi) {}

void ForecastPage::onEnter() {
    ESP_LOGI(TAG, "onEnter");
    _display.clearScreen();
    lastForecastPageKey = "";
}

void ForecastPage::update() {
    draw3DayForecast(
        _weather.getForecast(0),
        _weather.getForecast(1),
        _weather.getForecast(2)
    );
}

// 3 卡片 7 行布局（320×170）
//   y=0  : 标题 (城市 + 时间 + 更新)
//   y=22 : 卡片上边框
//   y=24 : 日期       (07-25 / 07-26 / 07-27)
//   y=46 : 天气       (多云 / 小雨 / 晴朗)
//   y=68 : 温度       (22° / 30°)
//   y=90 : 湿度       (湿80%)
//   y=112: 风向       (东北风)
//   y=134: 风力       (1-3级)
//   y=156: 卡片下边框 (cardH=134)
void ForecastPage::draw3DayForecast(const DailyForecast& day0, const DailyForecast& day1, const DailyForecast& day2) {
    String key = day0.date + day0.textDay + day0.tempMin + day0.tempMax +
                 day1.date + day1.textDay + day1.tempMin + day1.tempMax +
                 day2.date + day2.textDay + day2.tempMin + day2.tempMax;
    if (key == lastForecastPageKey) {
        return;
    }
    lastForecastPageKey = key;

    drawHeader();

    const DailyForecast days[3] = {day0, day1, day2};

    // 3 卡片并排: x 间距 4, 卡宽 100, 卡间隔 6
    // 4 + 100 + 6 + 100 + 6 + 100 + 4 = 320
    const int cardY = 22;
    const int cardH = 134;          // y=22-156 (7 行布局, 增加 8px 容纳风向+风力2行)
    const int cardW = 100;
    const int xs[3] = {4, 110, 216};

    for (int i = 0; i < 3; i++) {
        drawCard(xs[i], cardY, cardW, cardH, days[i]);
    }
}

void ForecastPage::drawHeader() {
    TFT_eSPI& tft = _display.getTFT();

    // 左侧：城市名
    String city = _weather.getCity();
    String cityLabel = city.length() > 0 ? ("[" + city + "]") : String("[--]");
    _display.drawTextXFont(cityLabel.c_str(), 4, 4, TFT_WHITE, TL_DATUM);

    // 右侧：HH:MM 更新（_time 未同步时显示 --:--）
    int year = _time.getYear();
    if (year > 2020) {
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d 更新", _time.getHour(), _time.getMinute());
        _display.drawTextXFont(timeStr, 316, 4, TFT_WHITE, TR_DATUM);
    } else {
        _display.drawTextXFont("--:-- 更新", 316, 4, TFT_WHITE, TR_DATUM);
    }
}

void ForecastPage::drawCard(int x, int y, int w, int h, const DailyForecast& day) {
    TFT_eSPI& tft = _display.getTFT();

    // 卡片上/下边框（细线）
    tft.drawFastHLine(x, y, w, TFT_DARKGREY);
    tft.drawFastHLine(x, y + h, w, TFT_DARKGREY);

    int centerX = x + w / 2;

    // 行 1 (y=24): 日期 "07-25"（取 fxDate 后 5 位）
    String dateText = day.date.length() >= 10 ? day.date.substring(5) : "--";
    _display.drawTextXFont(dateText.c_str(), centerX, y + 4, TFT_WHITE, TC_DATUM);

    // 行 2 (y=46): 天气文字
    String weatherText = day.textDay.length() > 0 ? day.textDay : "--";
    _display.drawTextXFont(weatherText.c_str(), centerX, y + 24, TFT_WHITE, TC_DATUM);

    // 行 3 (y=68): 温度 "22°/30°" — 用项目自定义的 COLOR_GOLD_WARM 暖金色
    String tempText;
    if (day.tempMin.length() > 0 && day.tempMax.length() > 0) {
        tempText = day.tempMin + "°/" + day.tempMax + "°";
    } else {
        tempText = "--°/--°";
    }
    _display.drawTextXFont(tempText.c_str(), centerX, y + 46, COLOR_GOLD_WARM, TC_DATUM);

    // 行 4 (y=90): 湿度 "湿80%"
    String humText = day.humidity.length() > 0
        ? ("湿" + day.humidity + "%")
        : "湿--%";
    _display.drawTextXFont(humText.c_str(), centerX, y + 68, TFT_WHITE, TC_DATUM);

    // 行 5 (y=112): 风向 "东北风"
    String windDirText;
    if (day.windDir.length() > 0) {
        windDirText = day.windDir + "风";
    } else {
        windDirText = "--风";
    }
    _display.drawTextXFont(windDirText.c_str(), centerX, y + 90, TFT_WHITE, TC_DATUM);

    // 行 6 (y=134): 风力等级 "1-3级"
    String windScaleText;
    if (day.windScale.length() > 0) {
        windScaleText = day.windScale + "级";
    } else {
        windScaleText = "--级";
    }
    _display.drawTextXFont(windScaleText.c_str(), centerX, y + 112, TFT_WHITE, TC_DATUM);
}
