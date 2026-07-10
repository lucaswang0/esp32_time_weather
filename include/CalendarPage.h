#ifndef CALENDAR_PAGE_H
#define CALENDAR_PAGE_H

#include "PageBase.h"

class DisplayManager;
class TimeManager;

/**
 * 月历页面：显示当前月份的日历
 */
class CalendarPage : public PageBase {
public:
    CalendarPage(DisplayManager& disp, TimeManager& time);

    void onEnter() override;
    void onExit() override {}
    void update() override;

private:
    DisplayManager& display;
    TimeManager& time;
};

#endif
