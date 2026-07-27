#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "WiFiManager.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "AHT20BMP280Sensor.h"
#include "PageManager.h"

class HistoryPage;
class PressurePage;
class TempPage;

class TaskManager {
public:
    TaskManager(
        WiFiManager& wifiManager,
        TimeManager& timeManager,
        WeatherManager& weatherManager,
        AHT20BMP280Sensor& sensor,
        HistoryPage* historyPage,
        PressurePage* pressurePage,
        TempPage* tempPage,
        PageManager& pageManager,
        SemaphoreHandle_t displayMutex
    );
    
    void begin();
    void stop();
    void printMemoryUsage(const char* tag = "snapshot");  // 打印所有任务栈 HWM + 堆状态
    
    bool isTimeSynced() const;
    void setTimeSynced(bool synced);
    
private:
    WiFiManager& _wifiManager;
    TimeManager& _timeManager;
    WeatherManager& _weatherManager;
    AHT20BMP280Sensor& _sensor;
    HistoryPage* _historyPage;
    PressurePage* _pressurePage;
    TempPage* _tempPage;
    PageManager& _pageManager;
    SemaphoreHandle_t _displayMutex;
    
    // 时间戳
    unsigned long _lastWiFiCheck = 0;
    unsigned long _lastTimeSync = 0;
    unsigned long _lastCurrentWeatherUpdate = 0;
    unsigned long _lastForecastUpdate = 0;
    unsigned long _lastTempRead = 0;
    unsigned long _lastHistorySave = 0;
    
    // 天气状态
    bool _ipLocationDone = false;
    bool _cityInfoDone = false;
    int _ipLocationFailCount = 0;
    unsigned long _lastIpLocationAttempt = 0;
    const unsigned long IP_RETRY_BASE_MS = 10 * 1000;    // 10s 起步
    const unsigned long IP_RETRY_MAX_MS = 5 * 60 * 1000;  // 最长 5 分钟
    const unsigned long CURRENT_WEATHER_MIN_INTERVAL_MS = 10 * 60 * 1000; // 当前天气10分钟最小间隔
    const unsigned long FORECAST_MIN_INTERVAL_MS = 60 * 60 * 1000;       // 天气预报1小时最小间隔
    
    // 时间同步状态
    volatile bool _timeSynced = false;
    bool _firstSyncAttempted = false;
    const unsigned long TIME_SYNC_INTERVAL_INITIAL = 5 * 60 * 1000;
    const unsigned long TIME_SYNC_INTERVAL_SUCCESS = 60 * 60 * 1000;
    
    // 任务句柄
    TaskHandle_t _taskWiFi = NULL;
    TaskHandle_t _taskTimeSync = NULL;
    TaskHandle_t _taskWeather = NULL;
    TaskHandle_t _taskSensors = NULL;
    TaskHandle_t _taskHistory = NULL;
    TaskHandle_t _taskTimeDisplay = NULL;
    
    // 互斥锁
    SemaphoreHandle_t _stateMutex = NULL;
    
    // 静态任务包装函数
    static void taskWiFiWrapper(void* pvParameters);
    static void taskTimeSyncWrapper(void* pvParameters);
    static void taskWeatherWrapper(void* pvParameters);
    static void taskSensorsWrapper(void* pvParameters);
    static void taskHistoryWrapper(void* pvParameters);
    static void taskTimeDisplayWrapper(void* pvParameters);
    
    // 实际任务逻辑
    void taskWiFi();
    void taskTimeSync();
    void taskWeather();
    void taskSensors();
    void taskHistory();
    void taskTimeDisplay();
};

#endif