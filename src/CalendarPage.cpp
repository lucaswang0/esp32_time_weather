#include "CalendarPage.h"
#include <esp_log.h>
#include "DisplayManager.h"
#include "TimeManager.h"

static const char* TAG = "CalendarPage";
CalendarPage::CalendarPage(DisplayManager& disp, TimeManager& time)
    : _display(disp), _time(time) {}

void CalendarPage::onEnter() {
    ESP_LOGI(TAG, "onEnter");
    _display.clearScreen();
    lastCalendarYear = -1;
    lastCalendarMonth = -1;
    lastCalendarDay = -1;
}

void CalendarPage::update() {
    int year = _time.getYear();
    int month = _time.getMonth();
    int day = _time.getDay();
    
    if (year == lastCalendarYear && month == lastCalendarMonth && day == lastCalendarDay) {
        return;
    }
    lastCalendarYear = year;
    lastCalendarMonth = month;
    lastCalendarDay = day;
    
    drawCalendar(year, month, day);
}

void CalendarPage::drawCalendar(int year, int month, int day) {
    TFT_eSPI& tft = _display.getTFT();
    
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
        daysInMonth[1] = 29;
    }
    
    int firstDayOfWeek = 0;
    {
        int y = year;
        int m = month;
        if (m < 3) {
            m += 12;
            y--;
        }
        firstDayOfWeek = (y + y/4 - y/100 + y/400 + (13*m + 8)/5 + 1) % 7;
    }
    
    int numDays = daysInMonth[month - 1];

    char title[20];
    sprintf(title, "%d年%d月", year, month);
    _display.drawTextCenterWithTransparentBgFont(title, 160, 2, TFT_WHITE, font_small_20);

    const char* weekDays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int i = 0; i < 7; i++) {
        _display.drawTextCenterWithTransparentBgFont(weekDays[i], 20 + i * 45, 22,
                                                     (i == 0 || i == 6) ? COLOR_GOLD_WARM : TFT_WHITE,
                                                     font_small_20);
    }

    int dayX = 20;
    int dayY = 42;
    int dayIndex = firstDayOfWeek;

    for (int i = 0; i < firstDayOfWeek; i++) {
        dayX += 45;
    }

    for (int d = 1; d <= numDays; d++) {
        bool isWeekend = (dayIndex == 0 || dayIndex == 6);
        char dayStr[4];
        sprintf(dayStr, "%d", d);

        if (d == day) {
            // 当天：先画圆再直接绘字，抗锯齿以圆圈色混合，避免擦掉圆圈
            tft.fillCircle(dayX, dayY + 9, 14, COLOR_PRIMARY);
            tft.loadFont(font_small_20);
            tft.setTextDatum(TC_DATUM);
            tft.setTextColor(TFT_WHITE, COLOR_PRIMARY);
            tft.drawString(dayStr, dayX, dayY);
            tft.unloadFont();
        } else {
            _display.drawTextCenterWithTransparentBgFont(dayStr, dayX, dayY,
                                                         isWeekend ? COLOR_GOLD_WARM : TFT_WHITE,
                                                         font_small_20);
        }

        dayX += 45;
        dayIndex++;

        if (dayIndex >= 7) {
            dayIndex = 0;
            dayX = 20;
            dayY += 22;
        }
    }
}
