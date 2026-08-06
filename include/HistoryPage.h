#ifndef HISTORY_PAGE_H
#define HISTORY_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "AHT20BMP280Sensor.h"

#define MAX_HISTORY_POINTS 144      // 单天最大数据点（24h × 6/h = 144）
#define MAX_HISTORY_DAYS 3          // 保留最近几天的数据
#define MAX_HISTORY_BUFFER (MAX_HISTORY_POINTS * MAX_HISTORY_DAYS)  // 总内存容量

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    time_t timestamp;
} WeatherRecord;

class HistoryPage : public PageBase {
public:
    HistoryPage(DisplayManager& disp, AHT20BMP280Sensor& aht20);
    ~HistoryPage() override;
    
    void onEnter() override;
    void onExit() override;
    void update() override;
    void addRecord(float temp, float humidity, float pressure);
    
private:
    DisplayManager& _display;
    AHT20BMP280Sensor& _aht20;
    
    WeatherRecord* history = nullptr;
    int historyCount;

    void drawStatusBar();
    void drawWeatherGraph();
    void drawBottomBar();
    void saveToLittleFS();
    void saveRecordToDailyFile(float temp, float humidity, float pressure);
    void loadFromLittleFS();
    void checkAndCleanOldFiles();
};

#endif