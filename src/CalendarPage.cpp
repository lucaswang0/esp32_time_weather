#include "CalendarPage.h"
#include "DisplayManager.h"
#include "TimeManager.h"

CalendarPage::CalendarPage(DisplayManager& disp, TimeManager& time)
    : display(disp), time(time) {}

void CalendarPage::onEnter() {
    Serial.println("[CalendarPage] onEnter");
    display.clearScreen();
    lastCalendarYear = -1;
    lastCalendarMonth = -1;
    lastCalendarDay = -1;
}

void CalendarPage::update() {
    int year = time.getYear();
    int month = time.getMonth();
    int day = time.getDay();
    
    if (year == lastCalendarYear && month == lastCalendarMonth && day == lastCalendarDay) {
        return;
    }
    lastCalendarYear = year;
    lastCalendarMonth = month;
    lastCalendarDay = day;
    
    drawCalendar(year, month, day);
}

void CalendarPage::drawCalendar(int year, int month, int day) {
    TFT_eSPI& tft = display.getTFT();
    
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

    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.loadFont(font_small_20);
    char title[20];
    sprintf(title, "%d年%d月", year, month);
    tft.drawString(title, 160, 5);
    tft.unloadFont();

    tft.loadFont(font_small_20);
    const char* weekDays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int i = 0; i < 7; i++) {
        tft.setTextColor((i == 0 || i == 6) ? COLOR_GOLD_WARM : TFT_WHITE);
        tft.drawString(weekDays[i], 20 + i * 45, 25);
    }

    int dayX = 20;
    int dayY = 45;
    int dayIndex = firstDayOfWeek;

    for (int i = 0; i < firstDayOfWeek; i++) {
        dayX += 45;
    }

    for (int d = 1; d <= numDays; d++) {
        tft.setTextDatum(TC_DATUM);
        bool isWeekend = (dayIndex == 0 || dayIndex == 6);
        if (d == day) {
            tft.fillCircle(dayX, dayY + 9, 14, COLOR_PRIMARY);
            tft.setTextColor(TFT_WHITE);
        } else if (isWeekend) {
            tft.setTextColor(COLOR_GOLD_WARM);
        } else {
            tft.setTextColor(TFT_WHITE);
        }

        char dayStr[4];
        sprintf(dayStr, "%d", d);
        tft.drawString(dayStr, dayX, dayY);

        dayX += 45;
        dayIndex++;

        if (dayIndex >= 7) {
            dayIndex = 0;
            dayX = 20;
            dayY += 27;
        }
    }

    tft.unloadFont();
}
