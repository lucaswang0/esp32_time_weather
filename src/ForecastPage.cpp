#include "ForecastPage.h"
#include <esp_log.h>
#include "DisplayManager.h"
#include "WeatherManager.h"
#include "WiFiManager.h"
#include "font_small_20.h"

static const char* TAG = "ForecastPage";

// 风向代码 → 中文描述映射（根据风向方位.md）
static String mapWindDir(const String& code) {
    if (code == "n")      return "北";
    if (code == "nne")    return "东北偏北";
    if (code == "ne")     return "东北";
    if (code == "ene")    return "东北偏东";
    if (code == "e")      return "东";
    if (code == "ese")    return "东南偏东";
    if (code == "se")     return "东南";
    if (code == "sse")    return "东南偏南";
    if (code == "s")      return "南";
    if (code == "ssw")    return "西南偏南";
    if (code == "sw")     return "西南";
    if (code == "wsw")    return "西南偏西";
    if (code == "w")      return "西";
    if (code == "wnw")    return "西北偏西";
    if (code == "nw")     return "西北";
    if (code == "nnw")    return "西北偏北";
    if (code == "none")   return "无持续风向";
    if (code == "vrb")    return "风向变化不定";
    return "";  // 未知代码返回空，后续显示 --
}
ForecastPage::ForecastPage(DisplayManager& disp, WeatherManager& weather, WiFiManager& wifi)
    : _display(disp), _weather(weather), _wifi(wifi) {}

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
    // 包含 fetchTime：每次成功获取都重新渲染"更新时间"
    String key = _weather.getForecastFetchTime() +
                 day0.date + day0.textDay + day0.tempMin + day0.tempMax +
                 day1.date + day1.textDay + day1.tempMin + day1.tempMax +
                 day2.date + day2.textDay + day2.tempMin + day2.tempMax;
    if (key == lastForecastPageKey) {
        return;
    }
    lastForecastPageKey = key;

    drawHeader(_weather.getForecastFetchTime());

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

void ForecastPage::drawHeader(const String& updateTime) {
    TFT_eSPI& tft = _display.getTFT();

    // 左侧：城市名
    String city = _weather.getCity();
    String cityStr = city.length() > 0 ? ("[" + city + "]") : "[--]";
    _display.drawTextWithTransparentBgFont(cityStr.c_str(), 4, 4, TFT_WHITE, font_small_20);

    // 右侧：分开绘制
    // 使用数据获取时间（WeatherManager 在成功 fetch 后记录的本地时间）
    char timeStr[8];
    if (updateTime.length() >= 5) {
        snprintf(timeStr, sizeof(timeStr), "%s", updateTime.c_str());
    } else {
        snprintf(timeStr, sizeof(timeStr), "--:--");
    }

    tft.loadFont(font_small_20);
    int textWidth = tft.textWidth(timeStr);
    tft.unloadFont();

    _display.drawTextWithTransparentBgFont(timeStr, 240 - textWidth, 4, TFT_WHITE, font_small_20);
    // 再绘制"更新"（中文字符）
    _display.drawTextWithTransparentBgFont("更新", 230 + textWidth, 4, TFT_WHITE, font_small_20);
}

void ForecastPage::drawCard(int x, int y, int w, int h, const DailyForecast& day) {
    TFT_eSPI& tft = _display.getTFT();

    // 卡片上/下边框（细线）
    tft.drawFastHLine(x, y, w, TFT_DARKGREY);
    tft.drawFastHLine(x, y + h, w, TFT_DARKGREY);

    int centerX = x + w / 2;

    // 行 1 (y=24): 日期 "07-25"（取 fxDate 后 5 位）
    String dateText = day.date.length() >= 10 ? day.date.substring(5) : "--";
    _display.drawTextCenterWithTransparentBgFont(dateText.c_str(), centerX, y + 4, TFT_WHITE, font_small_20);

    // 行 2 (y=46): 天气文字
    String weatherText = day.textDay.length() > 0 ? day.textDay : "--";
    _display.drawTextCenterWithTransparentBgFont(weatherText.c_str(), centerX, y + 26, TFT_WHITE, font_small_20);

    // 行 3 (y=68): 温度 "22°/30°" — 用项目自定义的 COLOR_GOLD_WARM 暖金色
    String tempText;
    if (day.tempMin.length() > 0 && day.tempMax.length() > 0) {
        float tempMinText = day.tempMin.toFloat();
        char tempMinStr[10];
        dtostrf(tempMinText, 0, 0, tempMinStr);
        float tempMaxText = day.tempMax.toFloat();
        char tempMaxStr[10];
        dtostrf(tempMaxText, 0, 0, tempMaxStr);
        tempText = String(tempMinStr) + "°/" + String(tempMaxStr) + "°";
    } else {
        tempText = "--°/--°";
    }
    _display.drawTextCenterWithTransparentBgFont(tempText.c_str(), centerX, y + 48, COLOR_GOLD_WARM, font_small_20);

    // 行 4 (y=90): 湿度 "湿80%"
    String humText = day.humidity.length() > 0
        ? ("湿" + day.humidity + "%")
        : "湿--%";
    _display.drawTextCenterWithTransparentBgFont(humText.c_str(), centerX, y + 70, TFT_WHITE, font_small_20);

    // 行 5 (y=112): 风向（方位代码 → 中文描述 + "风"）
    String windDirText;
    if (day.windDir.length() > 0) {
        String dirName = mapWindDir(day.windDir);
        if (dirName.length() > 0) {
            windDirText = dirName + "风";
        } else {
            windDirText = "--风";
        }
    } else {
        windDirText = "--风";
    }
    _display.drawTextCenterWithTransparentBgFont(windDirText.c_str(), centerX, y + 92, TFT_WHITE, font_small_20);

    // 行 6 (y=134): 风力等级 "1-3级"
    String windScaleText;
    if (day.windScale.length() > 0) {
        windScaleText = day.windScale + "级";
    } else {
        windScaleText = "--级";
    }
    _display.drawTextCenterWithTransparentBgFont(windScaleText.c_str(), centerX, y + 114, TFT_WHITE, font_small_20);
}
