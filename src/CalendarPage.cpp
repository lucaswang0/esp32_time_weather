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
    _display.drawTextXFont(title, 160, 5, TFT_WHITE, TC_DATUM);

    const char* weekDays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int i = 0; i < 7; i++) {
        uint16_t wc = (i == 0 || i == 6) ? COLOR_GOLD_WARM : TFT_WHITE;
        _display.drawTextXFont(weekDays[i], 20 + i * 45, 25, wc, TC_DATUM);
    }

    int dayX = 20;
    int dayY = 45;
    int dayIndex = firstDayOfWeek;

    for (int i = 0; i < firstDayOfWeek; i++) {
        dayX += 45;
    }

    for (int d = 1; d <= numDays; d++) {
        bool isWeekend = (dayIndex == 0 || dayIndex == 6);
        if (d == day) {
            tft.fillCircle(dayX, dayY + 9, 14, COLOR_PRIMARY);
        }
        uint16_t dc;
        if (d == day) dc = TFT_WHITE;
        else if (isWeekend) dc = COLOR_GOLD_WARM;
        else dc = TFT_WHITE;

        char dayStr[4];
        sprintf(dayStr, "%d", d);
        _display.drawTextXFont(dayStr, dayX, dayY, dc, TC_DATUM);

        dayX += 45;
        dayIndex++;

        if (dayIndex >= 7) {
            dayIndex = 0;
            dayX = 20;
            dayY += 22;
        }
    }
}
