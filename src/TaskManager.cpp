#include "TaskManager.h"
#include <Arduino.h>
#include "config.h"
#include "HistoryPage.h"
#include "PressurePage.h"
#include "TempPage.h"

TaskManager::TaskManager(
    WiFiManager& wifiManager,
    TimeManager& timeManager,
    WeatherManager& weatherManager,
    AHT20BMP280Sensor& sensor,
    HistoryPage* historyPage,
    PressurePage* pressurePage,
    TempPage* tempPage,
    PageManager& pageManager,
    SemaphoreHandle_t displayMutex
) : _wifiManager(wifiManager),
    _timeManager(timeManager),
    _weatherManager(weatherManager),
    _sensor(sensor),
    _historyPage(historyPage),
    _pressurePage(pressurePage),
    _tempPage(tempPage),
    _pageManager(pageManager),
    _displayMutex(displayMutex)
{
    _stateMutex = xSemaphoreCreateMutex();
}

void TaskManager::begin() {
    // WiFi 任务 - 10秒间隔
    xTaskCreatePinnedToCore(
        taskWiFiWrapper,
        "TaskWiFi",
        4096,
        this,
        3,
        &_taskWiFi,
        0
    );
    
    // 时间同步任务 - 5分钟/1小时间隔
    xTaskCreatePinnedToCore(
        taskTimeSyncWrapper,
        "TaskTimeSync",
        4096,
        this,
        4,
        &_taskTimeSync,
        0
    );
    
    // 天气更新任务 - 1小时间隔（但包含多个步骤）
    // 栈 16384 (16KB) 修复 Store access fault: HTTPS 期间 mbedTLS 握手 + JWT 生成
    // + 多 String 局部变量 累计栈使用峰值 6-8KB，8KB 容易在临界状态溢出踩坏其他内存
    xTaskCreatePinnedToCore(
        taskWeatherWrapper,
        "TaskWeather",
        16384,       // 8KB → 16KB（HTTPS 安全裕量）
        this,
        4,
        &_taskWeather,
        0
    );
    
    // 传感器读取任务 - 5秒间隔
    // 栈 4096 (4KB) 修复: 实测 2KB 栈使用率 94.7% (剩 108B)
    // 根因: Serial.printf 内部 vprintf 占用 ~1.5KB 栈, readAHT20 + readBMP280 两个 printf 累计 ~3KB
    // (加上 I2C 局部变量 + Wire 库 buffer 实际 ~1.9KB, 2KB 栈临界, 任何中断嵌套都会溢出)
    xTaskCreatePinnedToCore(
        taskSensorsWrapper,
        "TaskSensors",
        4096,        // 2KB → 4KB（4 倍安全裕量）
        this,
        5,
        &_taskSensors,
        1
    );
    
    // 历史记录任务 - 10分钟间隔
    xTaskCreatePinnedToCore(
        taskHistoryWrapper,
        "TaskHistory",
        4096,
        this,
        5,
        &_taskHistory,
        1
    );
    
    // 时间显示任务 - 100ms间隔
    xTaskCreatePinnedToCore(
        taskTimeDisplayWrapper,
        "TimeDisplay",
        8192,
        this,
        2,
        &_taskTimeDisplay,
        0
    );

    // 启动后等 1s 让所有任务稳定运行, 然后打印初始内存状态
    vTaskDelay(pdMS_TO_TICKS(1000));
    printMemoryUsage("startup");
}

void TaskManager::printMemoryUsage(const char* tag) {
    // 【优化#8】统一打印所有任务栈 HWM + 堆状态, 用于内存优化决策
    // HWM = 任务栈剩余最小值 (从栈基址往下)
    // used = 分配大小 - HWM (实际使用峰值)
    // ⚠️ = used > 80% (危险), ⚡ = used > 60% (警告), ✓ = 正常
    Serial.printf("\n=== 内存快照 [%s] ===\n", tag);

    struct TaskInfo {
        TaskHandle_t handle;
        const char* name;
        uint32_t size;
    };
    TaskInfo tasks[] = {
        {_taskWiFi,        "TaskWiFi",     4096},
        {_taskTimeSync,    "TaskTimeSync", 4096},
        {_taskWeather,     "TaskWeather",  16384},
        {_taskSensors,     "TaskSensors",  4096},
        {_taskHistory,     "TaskHistory",  4096},
        {_taskTimeDisplay, "TimeDisplay",  8192},
    };

    uint32_t totalAlloc = 0;
    uint32_t totalUsed  = 0;
    for (const auto& t : tasks) {
        if (t.handle == NULL) {
            Serial.printf("  %-15s : [未创建]\n", t.name);
            continue;
        }
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(t.handle);
        uint32_t used = t.size - hwm;
        float pct = (t.size > 0) ? (used * 100.0f / t.size) : 0.0f;
        const char* mark = (pct > 80) ? " ⚠️" : (pct > 60) ? " ⚡" : " ✓";
        Serial.printf("  %-15s : %5u B 剩 / %5u B 分配 | 用了 ~%5u B (%4.1f%%)%s\n",
                      t.name, hwm, t.size, used, pct, mark);
        totalAlloc += t.size;
        totalUsed  += used;
    }
    float totalPct = (totalAlloc > 0) ? (totalUsed * 100.0f / totalAlloc) : 0.0f;
    Serial.printf("  %-15s : %5u B 总用 / %5u B 总分配 (%4.1f%%)\n",
                  "[任务栈合计]", totalUsed, totalAlloc, totalPct);
    Serial.printf("  %-15s : %u B 剩余, 最大连续块: %u B\n",
                  "[堆]", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    Serial.println("========================\n");
}

void TaskManager::stop() {
    if (_taskWiFi) {
        vTaskDelete(_taskWiFi);
        _taskWiFi = NULL;
    }
    if (_taskTimeSync) {
        vTaskDelete(_taskTimeSync);
        _taskTimeSync = NULL;
    }
    if (_taskWeather) {
        vTaskDelete(_taskWeather);
        _taskWeather = NULL;
    }
    if (_taskSensors) {
        vTaskDelete(_taskSensors);
        _taskSensors = NULL;
    }
    if (_taskHistory) {
        vTaskDelete(_taskHistory);
        _taskHistory = NULL;
    }
    if (_taskTimeDisplay) {
        vTaskDelete(_taskTimeDisplay);
        _taskTimeDisplay = NULL;
    }
}

bool TaskManager::isTimeSynced() const {
    return _timeSynced;
}

void TaskManager::setTimeSynced(bool synced) {
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _timeSynced = synced;
        xSemaphoreGive(_stateMutex);
    }
}

// ==================== 静态任务包装函数 ====================

void TaskManager::taskWiFiWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskWiFi();
}

void TaskManager::taskTimeSyncWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskTimeSync();
}

void TaskManager::taskWeatherWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskWeather();
}

void TaskManager::taskSensorsWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskSensors();
}

void TaskManager::taskHistoryWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskHistory();
}

void TaskManager::taskTimeDisplayWrapper(void* pvParameters) {
    TaskManager* manager = reinterpret_cast<TaskManager*>(pvParameters);
    manager->taskTimeDisplay();
}

// ==================== 任务逻辑实现 ====================

void TaskManager::taskWiFi() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    
    for (;;) {
        unsigned long now = millis();
        
        if (now - _lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            _lastWiFiCheck = now;
            
            if (_wifiManager.isAPStarted()) {
                _wifiManager.handleClient();
                if (_wifiManager.isConnected()) {
                    Serial.println("[TaskWiFi] WiFi connected via config portal!");
                    _wifiManager.stopAPMode();
                    _lastCurrentWeatherUpdate = 0;
                    _lastForecastUpdate = 0;
                } else if (_pageManager.current() != PageManager::PAGE_AP_MODE) {
                    Serial.println("[TaskWiFi] Left AP page, stopping AP and reconnecting WiFi...");
                    _wifiManager.stopAPMode();
                }
            } else if (_pageManager.current() == PageManager::PAGE_AP_MODE) {
                Serial.println("[TaskWiFi] AP mode stopped, switching back to temp page...");
                if (xSemaphoreTake(_displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    _pageManager.switchTo(PageManager::PAGE_TEMP);
                    xSemaphoreGive(_displayMutex);
                }
            } else if (_wifiManager.isConnected()) {
                _wifiManager.maintainConnection();
            } else {
                Serial.println("[TaskWiFi] WiFi not connected, attempting connection...");
                if (_wifiManager.connect()) {
                    Serial.println("[TaskWiFi] WiFi connection successful!");
                    _lastCurrentWeatherUpdate = 0;
                    _lastForecastUpdate = 0;
                }
            }
        }
        
        _wifiManager.checkAPTimeout();
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::taskTimeSync() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);
    
    for (;;) {
        if (!_wifiManager.isConnected()) {
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }
        
        unsigned long now = millis();
        bool synced = _timeSynced;
        
        if (!synced) {
            if (!_firstSyncAttempted) {
                Serial.println("[TaskTimeSync] 首次时间同步尝试");
                if (_timeManager.sync()) {
                    setTimeSynced(true);
                    _lastTimeSync = now;
                    Serial.println("[TaskTimeSync] 首次时间同步成功");
                } else {
                    _firstSyncAttempted = true;
                    _lastTimeSync = now;
                    Serial.println("[TaskTimeSync] 首次时间同步失败，将每5分钟重试");
                }
            } else {
                if (now - _lastTimeSync >= TIME_SYNC_INTERVAL_INITIAL) {
                    _lastTimeSync = now;
                    Serial.println("[TaskTimeSync] 时间同步重试...");
                    if (_timeManager.sync()) {
                        setTimeSynced(true);
                        Serial.println("[TaskTimeSync] 时间同步成功");
                    } else {
                        Serial.println("[TaskTimeSync] 时间同步失败，继续重试");
                    }
                }
            }
        } else {
            if (now - _lastTimeSync >= TIME_SYNC_INTERVAL_SUCCESS) {
                _lastTimeSync = now;
                _timeManager.sync();
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::taskWeather() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    for (;;) {
        unsigned long now = millis();

        // HWM 监控辅助：打印任务栈剩余最小值 + 堆状态
        // 正常情况: HWM > 12KB；告警: HWM < 4KB (栈快吃完)
        // 堆正常: freeHeap > 70KB, maxAllocHeap > 30KB
        auto printHwm = [this](const char* stage) {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            if (hwm < 4096) {
                Serial.printf("[TaskWeather] ⚠️  HWM after %s: %u bytes free (栈告警 < 4KB)\n", stage, hwm);
            } else {
                Serial.printf("[TaskWeather] HWM after %s: %u bytes free\n", stage, hwm);
            }
            // 每 3 个阶段打一次完整内存快照 (避免日志刷屏)
            // 用 stage 名字后缀区分, 只在 stage2/stage4 (偶数) 打全量
            size_t freeHeap = ESP.getFreeHeap();
            size_t maxAlloc = ESP.getMaxAllocHeap();
            Serial.printf("[TaskWeather] 堆状态 after %s: %u B 剩余, 最大块 %u B\n",
                          stage, freeHeap, maxAlloc);
        };

        // 【优化#5】堆碎片化检测：连续堆最大块 < 24KB 时主动回收 + 等待
        // mbedTLS 内部需要 ~16KB 连续 + 4KB 响应 buffer = 20KB+，getMaxAllocHeap < 24KB
        // 时调用 mbedTLS 必然失败 (-32512)。等待最 15s 让 FreeRTOS 回收碎片
        auto waitForHeap = [](size_t needBytes) -> bool {
            const int maxWait = 15;  // 15s
            for (int i = 0; i < maxWait; i++) {
                size_t maxAlloc = ESP.getMaxAllocHeap();
                if (maxAlloc >= needBytes) {
                    if (i > 0) {
                        Serial.printf("[TaskWeather] 堆已恢复, 最大连续块: %u B (等待 %ds)\n",
                                      maxAlloc, i);
                    }
                    return true;
                }
                if (i == 0 || i == 5 || i == 10) {
                    Serial.printf("[TaskWeather] 堆碎片化, 最大连续块: %u B, 需 %u B, 等待中...\n",
                                  maxAlloc, needBytes);
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            Serial.printf("[TaskWeather] 堆 15s 内未恢复 (最大块 %u B), 放弃本阶段\n",
                          ESP.getMaxAllocHeap());
            return false;
        };

        // 阶段一：IP定位（仅获取一次，成功后不再获取）
        if (!_ipLocationDone && _wifiManager.isConnected()) {
            if (!waitForHeap(24 * 1024)) {
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
                continue;
            }
            Serial.println("[TaskWeather] 阶段一: 通过IP获取定位");
            if (_weatherManager.fetchLocationByIP()) {
                Serial.println("[TaskWeather] IP定位成功");
                _ipLocationDone = true;
            } else {
                Serial.println("[TaskWeather] IP定位失败，下次任务时重试");
            }
            printHwm("stage1 IP-location");
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }

        // 阶段二：城市信息（IP定位成功后仅获取一次）
        if (!_cityInfoDone && _wifiManager.isConnected()) {
            if (!waitForHeap(24 * 1024)) {
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
                continue;
            }
            Serial.println("[TaskWeather] 阶段二: 获取城市信息");
            if (_weatherManager.fetchCityInfo()) {
                Serial.println("[TaskWeather] 城市信息获取成功");
                _cityInfoDone = true;
            } else {
                Serial.println("[TaskWeather] 城市信息获取失败，下次任务时重试");
            }
            printHwm("stage2 city-info");
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }

        // 阶段三：获取当前天气（成功后需间隔10分钟以上）
        bool currentWeatherEmpty = (_weatherManager.getTemperature().length() == 0 ||
                                   _weatherManager.getWeatherText().length() == 0);

        if (currentWeatherEmpty || now - _lastCurrentWeatherUpdate >= CURRENT_WEATHER_MIN_INTERVAL_MS) {
            if (_wifiManager.isConnected()) {
                if (!waitForHeap(24 * 1024)) {
                    vTaskDelayUntil(&xLastWakeTime, xFrequency);
                    continue;
                }
                Serial.println("[TaskWeather] 阶段三: 获取当前天气");
                if (_weatherManager.fetchCurrentWeather()) {
                    Serial.println("[TaskWeather] 当前天气获取成功");
                    _lastCurrentWeatherUpdate = now;
                } else {
                    Serial.println("[TaskWeather] 当前天气获取失败");
                }
                printHwm("stage3 current-weather");
            }
        }

        // 阶段四：获取天气预报（成功后需间隔1小时以上）
        bool forecastEmpty = (_weatherManager.getForecast(0).date.length() == 0);

        if (forecastEmpty || now - _lastForecastUpdate >= FORECAST_MIN_INTERVAL_MS) {
            if (_wifiManager.isConnected()) {
                if (!waitForHeap(24 * 1024)) {
                    vTaskDelayUntil(&xLastWakeTime, xFrequency);
                    continue;
                }
                Serial.println("[TaskWeather] 阶段四: 获取天气预报");
                if (_weatherManager.fetch3DayForecast()) {
                    Serial.println("[TaskWeather] 天气预报获取成功");
                    _lastForecastUpdate = now;
                } else {
                    Serial.println("[TaskWeather] 天气预报获取失败");
                }
                printHwm("stage4 forecast");
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::taskSensors() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(500);
    
    for (;;) {
        unsigned long now = millis();
        
        if (now - _lastTempRead >= TEMP_READ_INTERVAL) {
            _lastTempRead = now;
            _sensor.update();
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::taskHistory() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);
    
    for (;;) {
        if (!_timeSynced) {
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }
        
        unsigned long now = millis();
        
        if (_lastHistorySave == 0) {
            if (_timeManager.getYear() > 2020) {
                int currentMinute = _timeManager.getHour() * 60 + _timeManager.getMinute();
                int nextSlotMinute = ((currentMinute / 10) + 1) * 10;
                int delayToNextSlot = (nextSlotMinute - currentMinute) * 60000;
                _lastHistorySave = millis() - (unsigned long)(600000 - delayToNextSlot);
                Serial.printf("[TaskHistory] NTP同步成功，历史保存首次对齐到 %02d:%02d（%d 分钟后）\n",
                              (nextSlotMinute / 60) % 24, nextSlotMinute % 60,
                              delayToNextSlot / 60000);
            } else {
                _lastHistorySave = millis();
            }
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }
        
        if (now - _lastHistorySave >= 600000) {
            _lastHistorySave = now;
            if (_sensor.isValid()) {
                if (_historyPage != nullptr) {
                    _historyPage->addRecord(
                        _sensor.getTemperature(),
                        _sensor.getHumidity(),
                        _sensor.getPressure()
                    );
                    Serial.printf("[TaskHistory] 保存传感器数据: 温度=%.1f°C 湿度=%.1f%% 气压=%.1fhPa\n",
                        _sensor.getTemperature(),
                        _sensor.getHumidity(),
                        _sensor.getPressure());
                }
                
                if (_pressurePage != nullptr && _pressurePage->checkAlert()) {
                    Serial.println("[TaskHistory] 气压警告触发，自动切换到气压页面！");
                    if (xSemaphoreTake(_displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        _pageManager.switchTo(PageManager::PAGE_PRESSURE);
                        xSemaphoreGive(_displayMutex);
                    }
                }
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::taskTimeDisplay() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);

    for (;;) {
        if (xSemaphoreTake(_displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            _timeManager.update();
            if (_pageManager.current() == PageManager::PAGE_TEMP && _tempPage != nullptr) {
                _tempPage->updateTime(
                    _timeManager.getYear(),
                    _timeManager.getMonth(),
                    _timeManager.getDay(),
                    _timeManager.getHour(),
                    _timeManager.getMinute(),
                    _timeManager.getSecond(),
                    _timeManager.getWeekday()
                );
            }
            xSemaphoreGive(_displayMutex);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}