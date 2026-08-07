#include "TimeManager.h"
#include <esp_log.h>
#include "config.h"

static const char* TAG = "Time";

TimeManager::TimeManager(WiFiManager& wifiManager) : wifiManager(wifiManager),
    year(0), month(0), day(0), hour(0), minute(0), second(0), weekday(0) {}

bool TimeManager::sync() {
    if (!wifiManager.isConnected()) {
        ESP_LOGI(TAG, "[NTP] WiFi not connected, skip sync");
        return false;
    }
    
    ESP_LOGI(TAG, "[NTP] Syncing time...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               "ntp.aliyun.com", "pool.ntp.org", "time.google.com");
    
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo, 0) && retry < 10) {
        delay(1000);
        ESP_LOGI(TAG, ".");
        retry++;
    }
    
    if (retry < 10) {
        year = timeinfo.tm_year + 1900;
        month = timeinfo.tm_mon + 1;
        day = timeinfo.tm_mday;
        hour = timeinfo.tm_hour;
        minute = timeinfo.tm_min;
        second = timeinfo.tm_sec;
        weekday = timeinfo.tm_wday;
        ESP_LOGI(TAG, " ✅");
        ESP_LOGI(TAG, "   Time: %04d-%02d-%02d %02d:%02d:%02d",
                      year, month, day, hour, minute, second);
        return true;
    }
    ESP_LOGE(TAG, " ❌");
    return false;
}

void TimeManager::update() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        year = timeinfo.tm_year + 1900;
        month = timeinfo.tm_mon + 1;
        day = timeinfo.tm_mday;
        hour = timeinfo.tm_hour;
        minute = timeinfo.tm_min;
        second = timeinfo.tm_sec;
        weekday = timeinfo.tm_wday;
    }
}

int TimeManager::getYear() const { return year; }
int TimeManager::getMonth() const { return month; }
int TimeManager::getDay() const { return day; }
int TimeManager::getHour() const { return hour; }
int TimeManager::getMinute() const { return minute; }
int TimeManager::getSecond() const { return second; }
int TimeManager::getWeekday() const { return weekday; }