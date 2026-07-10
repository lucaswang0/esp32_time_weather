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
    
    void onEnter() override;
    void onExit() override {}
    void update() override;
    void addRecord(float temp, float humidity, float pressure);
    
private:
    DisplayManager& display;
    AHT20BMP280Sensor& aht20;
    
    WeatherRecord history[MAX_HISTORY_BUFFER];
    int historyCount;

    void drawStatusBar();
    void drawWeatherGraph();
    void drawBottomBar();
    void saveToSPIFFS();
    void saveRecordToDailyFile(float temp, float humidity, float pressure);
    void loadFromSPIFFS();
    void checkAndCleanOldFiles();
};

#endif