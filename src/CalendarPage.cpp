#include "CalendarPage.h"
#include "DisplayManager.h"
#include "TimeManager.h"

CalendarPage::CalendarPage(DisplayManager& disp, TimeManager& time)
    : display(disp), time(time) {}

void CalendarPage::onEnter() {
    Serial.println("[CalendarPage] onEnter");
    display.clearScreen();
    display.resetCache();
}

void CalendarPage::update() {
    display.displayCalendar(
        time.getYear(),
        time.getMonth(),
        time.getDay()
    );
}
