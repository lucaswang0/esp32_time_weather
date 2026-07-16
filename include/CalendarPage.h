#ifndef CALENDAR_PAGE_H
#define CALENDAR_PAGE_H

#include "PageBase.h"

class DisplayManager;
class TimeManager;

class CalendarPage : public PageBase {
public:
    CalendarPage(DisplayManager& disp, TimeManager& time);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& display;
    TimeManager& time;
    
    int lastCalendarYear = -1;
    int lastCalendarMonth = -1;
    int lastCalendarDay = -1;
    
    void drawCalendar(int year, int month, int day);
};

#endif
