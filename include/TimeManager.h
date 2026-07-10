#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <time.h>
#include "WiFiManager.h"

class TimeManager {
public:
    TimeManager(WiFiManager& wifiManager);
    bool sync();
    void update();
    int getYear() const;
    int getMonth() const;
    int getDay() const;
    int getHour() const;
    int getMinute() const;
    int getSecond() const;
    int getWeekday() const;

private:
    WiFiManager& wifiManager;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
};

#endif