#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "DisplayManager.h"

#include "AHT20BMP280Sensor.h"
#include "LEDController.h"
#include "BuzzerController.h"
#include "TTP223Sensor.h"
#include "PageBase.h"
#include "PageManager.h"
#include "TempPage.h"
#include "CalendarPage.h"
#include "ForecastPage.h"
#include "PressurePage.h"
#include "HistoryPage.h"
#include "WiFiInfoPage.h"
#include "APModePage.h"
#include "StreamingPlayerPage.h"
#include "FlipClockPage.h"
#include <driver/gpio.h>

// ==================== 全局对象 ====================

BuzzerController buzzerController(PIN_BUZZER);
WiFiManager wifiManager;
TimeManager timeManager(wifiManager);
WeatherManager weatherManager(wifiManager);
DisplayManager displayManager;
AHT20BMP280Sensor aht20Bmp280Sensor(PIN_I2C_SDA, PIN_I2C_SCL);
LEDController ledController(PIN_LED_D4);
TTP223Sensor touchSensor(PIN_TOUCH);

// ==================== 页面对象（指针形式，不驻留 .bss） ====================

TempPage*            pTempPage            = nullptr;
CalendarPage*        pCalendarPage        = nullptr;
ForecastPage*        pForecastPage        = nullptr;
PressurePage*        pPressurePage        = nullptr;
HistoryPage*         pHistoryPage         = nullptr;
WiFiInfoPage*        pWiFiInfoPage        = nullptr;
StreamingPlayerPage* pStreamingPlayerPage = nullptr;
APModePage*          pAPModePage          = nullptr;
FlipClockPage*       pFlipClockPage       = nullptr;
PageManager          pageManager(displayManager);

// Arduino core 的 getArduinoLoopTaskStackSize() 是 weak，默认返回 8K。
// 这里 override 为 32K，避免 HistoryPage/PressurePage 绘制链把栈踩穿。
// Arduino.h 把它声明成 C++ linkage（C++ 函数允许 weak override），所以不要用 extern "C"。
size_t getArduinoLoopTaskStackSize(void) {
    return 32768;
}

// ==================== 背光 ====================

const int BACKLIGHT_CHANNEL = 0;
const int BUZZER_CHANNEL = 1;
const int BACKLIGHT_LEVELS[] = {10, 80, 160, 255};
int currentBacklightLevel = 2;

// ==================== 时间戳 ====================

unsigned long lastWiFiCheck = 0;
unsigned long lastTimeSync = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastTempRead = 0;
unsigned long lastLEDConditionCheck = 0;
unsigned long lastAutoBrightnessCheck = 0;
unsigned long lastHistorySave = 0;

int weatherStep = 0;
int weatherRetryCount = 0;
const int MAX_WEATHER_RETRIES = 5;
const unsigned long WEATHER_BACKOFF_MS = 300000;
unsigned long weatherBackoffEnd = 0;
int lastAutoBrightnessDay = -1;
SemaphoreHandle_t displayMutex = NULL;

bool timeSynced = false;
bool firstSyncAttempted = false;
const unsigned long TIME_SYNC_INTERVAL_INITIAL = 5 * 60 * 1000;
const unsigned long TIME_SYNC_INTERVAL_SUCCESS = 60 * 60 * 1000;

// ==================== FreeRTOS 时间任务声明 ====================
void TaskTimeDisplay(void *pvParameters);

// ==================== 背光与触摸辅助函数 ====================

static void setBacklightLevel(int level) {
    // 先分离，再重新附加，避免 LEDC 通道卡住
    ledcDetachPin(PIN_TFT_BL);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, level);
}

static int parseTimeToMinutes(const String& timeStr) {
    if (timeStr.length() < 5) return -1;
    int colon = timeStr.indexOf(':');
    if (colon <= 0) return -1;
    int hour = timeStr.substring(0, colon).toInt();
    int minute = timeStr.substring(colon + 1).toInt();
    return hour * 60 + minute;
}

static int calculateAutoBrightness() {
    const DailyForecast& today = weatherManager.getForecast(0);
    int sunrise = parseTimeToMinutes(today.sunrise);
    int sunset = parseTimeToMinutes(today.sunset);
    
    if (sunrise < 0 || sunset < 0) {
        Serial.println("[Brightness] 日出日落数据不可用");
        return -1;
    }
    
    int nowMinutes = timeManager.getHour() * 60 + timeManager.getMinute();
    int sunriseMinus30 = sunrise - 30;
    int sunsetPlus30 = sunset + 30;
    
    Serial.printf("[Brightness] 日出:%d 日落:%d 当前:%d\n", sunrise, sunset, nowMinutes);
    
    if (nowMinutes >= sunriseMinus30 && nowMinutes < sunset) {
        return 2;
    } else if (nowMinutes >= sunset && nowMinutes < sunsetPlus30) {
        return 1;
    } else {
        return 0;
    }
}

static void handleTouchEvent(TouchType type) {
    switch (type) {
        case TOUCH_SHORT: {
            Serial.println("[Touch] Short touch - next page");
            //播放触摸反馈音（短促清脆）
            buzzerController.touchFeedbackShort();

            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pageManager.next();
                pageManager.dispatchTouch(TOUCH_SHORT_BASE);
                xSemaphoreGive(displayMutex);
            }
            break;
        }
        case TOUCH_DOUBLE: {
            Serial.println("[Touch] Double touch - prev page");
            //播放触摸反馈音（双击嘀嘀）
            buzzerController.touchFeedbackDouble();

            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pageManager.prev();
                pageManager.dispatchTouch(TOUCH_DOUBLE_BASE);
                xSemaphoreGive(displayMutex);
            }
            break;
        }
        // case TOUCH_LONG: {
        //     Serial.println("[Touch] Long touch - previous page");
        //     //播放触摸反馈音（长促嘟——）
        //     buzzerController.touchFeedbackLong();

        //     if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        //         // pageManager.prev();
        //         pageManager.dispatchTouch(TOUCH_LONG_BASE);
        //         xSemaphoreGive(displayMutex);
        //     }
        //     break;
        // }
        case TOUCH_VERY_LONG:
            Serial.println("[Touch] Very long touch - Enter AP mode");
            //播放触摸反馈音（长促嘟——）
            buzzerController.touchFeedbackLong();

            WiFi.disconnect(true);
            delay(100);
            wifiManager.startAPMode();
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pageManager.switchTo(PageManager::PAGE_AP_MODE);
                xSemaphoreGive(displayMutex);
            }
            break;

        default:
            break;
    }
}

// ==================== setup ====================

void setup() {
    // 最优先：关闭蜂鸣器（低电平触发，高电平关闭）
    // pinMode(PIN_BUZZER, OUTPUT);
    // digitalWrite(PIN_BUZZER, HIGH);

    // ========== 1. 蜂鸣器PWM初始化（最优先） ==========
    // 1. 先配置PWM参数
    ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
    // 2. 在绑定引脚之前，先把占空比设为0
    ledcWrite(LEDC_CHANNEL, 0);
    // 3. 再绑定引脚（此时通道已经是0%占空比）
    ledcAttachPin(PIN_BUZZER, LEDC_CHANNEL);

      // ========== 2. 串口初始化 ==========
    Serial.begin(115200);
    delay(500);

      // ========== 3. 挂载 SPIFFS ==========
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] Mount Failed - Formatting...");
        if (!SPIFFS.begin(true)) {
            Serial.println("[SPIFFS] Format Failed");
        } else {
            Serial.println("[SPIFFS] Format Success");
        }
    } else {
        Serial.println("[SPIFFS] Mount Success");
    }

   // ========== 4. 背光PWM初始化 ==========
    ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, BACKLIGHT_LEVELS[currentBacklightLevel]);

  // ========== 5. 屏幕初始化 ==========
    displayManager.init();

  // ========== 6. 屏幕初始化后，重新附加背光PWM ==========
    // 先确保引脚高电平，避免闪烁
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
    
    ledcDetachPin(PIN_TFT_BL);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, BACKLIGHT_LEVELS[currentBacklightLevel]);
    Serial.printf("[Backlight] 重新附加背光 PWM, 等级: %d\n", currentBacklightLevel);

 // ========== 7. 屏幕初始化后，重新附加蜂鸣器PWM ==========
    // TFT_eSPI.init() 可能重置了GPIO状态，所以重新附加
    // 注意：不需要再 pinMode + digitalWrite，因为 ledcAttachPin 会接管引脚
    ledcDetachPin(PIN_BUZZER);
    ledcAttachPin(PIN_BUZZER, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, (1 << LEDC_TIMER_BIT) - 1);
    Serial.println("[Main] 蜂鸣器 PWM 重新附加，静音状态（高电平）");

    // AHT20+BMP280
    if (!aht20Bmp280Sensor.begin()) {
        Serial.println("[Main] AHT20+BMP280 传感器初始化失败");
    } else {
        Serial.println("[Main] AHT20+BMP280 传感器初始化成功");
        delay(100);
        aht20Bmp280Sensor.update();
    }


    // LED
    ledController.begin();

    // Buzzer
    buzzerController.begin();

    // 触摸
    touchSensor.begin();
    Serial.println("[Main] TTP223 touch sensor initialized");

    // 互斥锁
    displayMutex = xSemaphoreCreateMutex();

    Serial.println("\n================================================");
    Serial.println("   ESP32-C3 Weather Clock (Page-based)");
    Serial.println("================================================\n");

    wifiManager.connect();
    timeManager.update();

    if (wifiManager.isConnected()) {
        Serial.println("[Main] WiFi连接成功，开始时间同步");
        if (timeManager.sync()) {
            timeSynced = true;
            Serial.println("[Main] NTP时间同步成功");
        } else {
            Serial.println("[Main] NTP时间同步失败，将在循环中重试");
        }
        firstSyncAttempted = true;
        lastWeatherUpdate = millis();
    } else {
        Serial.println("[Main] WiFi连接失败，开启AP配网模式");
        wifiManager.startAPMode();
    }

    // 启动页面管理器
    pTempPage     = new TempPage(displayManager, weatherManager, aht20Bmp280Sensor, wifiManager);
    pCalendarPage = new CalendarPage(displayManager, timeManager);
    pForecastPage = new ForecastPage(displayManager, weatherManager);
    pPressurePage = new PressurePage(displayManager, aht20Bmp280Sensor);
    pHistoryPage            = new HistoryPage(displayManager, aht20Bmp280Sensor);
    pAPModePage            = new APModePage(displayManager, wifiManager);
    pWiFiInfoPage          = new WiFiInfoPage(displayManager, wifiManager);
    pStreamingPlayerPage   = new StreamingPlayerPage(displayManager);
    pFlipClockPage         = new FlipClockPage(displayManager);
    pageManager.registerPage(PageManager::PAGE_TEMP,         pTempPage);
    pageManager.registerPage(PageManager::PAGE_FORECAST,     pForecastPage);
    pageManager.registerPage(PageManager::PAGE_CALENDAR,     pCalendarPage);
    pageManager.registerPage(PageManager::PAGE_PRESSURE,     pPressurePage);
    pageManager.registerPage(PageManager::PAGE_HISTORY,      pHistoryPage);
    pageManager.registerPage(PageManager::PAGE_WIFI_INFO,    pWiFiInfoPage);
    pageManager.registerPage(PageManager::PAGE_AP_MODE,      pAPModePage);
    pageManager.registerPage(PageManager::PAGE_STREAMING,    pStreamingPlayerPage);
    pageManager.registerPage(PageManager::PAGE_FLIP_CLOCK,   pFlipClockPage);
    
    pageManager.begin();

    // 初始化传感器定时器时间戳
    lastTempRead = millis();

    // 历史保存仅在NTP同步后才初始化
    if (timeSynced && timeManager.getYear() > 2020) {
        int currentMinute = timeManager.getHour() * 60 + timeManager.getMinute();
        int nextSlotMinute = ((currentMinute / 10) + 1) * 10;
        int delayToNextSlot = (nextSlotMinute - currentMinute) * 60000;
        lastHistorySave = millis() - (unsigned long)(600000 - delayToNextSlot);
        Serial.printf("[Main] 历史保存首次对齐到 %02d:%02d（%d 分钟后）\n",
                      (nextSlotMinute / 60) % 24, nextSlotMinute % 60,
                      delayToNextSlot / 60000);
    } else {
        lastHistorySave = 0;
        Serial.println("[Main] NTP未同步，历史保存暂不开启");
    }

    // Serial.println("\n[Main] ===== 模拟钟声测试 =====");
    // Serial.println("[Main] Testing radioChime (1 times)...");
    // buzzerController.radioChime();
    // delay(5000);
    // Serial.println("[Main] 钟声测试完成");



    // 时间显示任务
    xTaskCreatePinnedToCore(
        TaskTimeDisplay,
        "TimeDisplay",
        8192,
        NULL,
        2,
        NULL,
        0
    );
   
    // 播放启动自检声
    buzzerController.startupChime();
    
}

// ==================== 时间显示任务（仅温度页面需要） ====================

void TaskTimeDisplay(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);

    for (;;) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            timeManager.update();
            if (pageManager.current() == PageManager::PAGE_TEMP && pTempPage != nullptr) {
                pTempPage->updateTime(
                    timeManager.getYear(),
                    timeManager.getMonth(),
                    timeManager.getDay(),
                    timeManager.getHour(),
                    timeManager.getMinute(),
                    timeManager.getSecond(),
                    timeManager.getWeekday()
                );
            }
            xSemaphoreGive(displayMutex);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==================== loop 功能函数声明 ====================

void handleTouch();
void handleWiFi(unsigned long now);
void handleTimeSync(unsigned long now);
void handleWeather(unsigned long now);
void handleSensors(unsigned long now);
void handleHistory(unsigned long now);
void handleDisplay();
void handleLED(unsigned long now);
void handleChime();
void handleBrightness(unsigned long now);

// ==================== loop ====================

void loop() {
    unsigned long now = millis();
    handleTouch();
    handleWiFi(now);
    handleTimeSync(now);
    handleWeather(now);
    handleSensors(now);
    handleHistory(now);
    handleDisplay();
    handleLED(now);
    handleChime();
    handleBrightness(now);
}

// ==================== loop 功能函数实现 ====================

void handleTouch() {
    touchSensor.update();
    if (touchSensor.hasNewTouch()) {
        handleTouchEvent(touchSensor.getLastTouchType());
        touchSensor.clearTouchEvent();
    }
}

void handleWiFi(unsigned long now) {
    if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = now;
        if (wifiManager.isAPStarted()) {
            wifiManager.handleClient();
            if (wifiManager.isConnected()) {
                Serial.println("[Main] WiFi connected via config portal!");
                wifiManager.stopAPMode();
                lastWeatherUpdate = now;
            } else if (pageManager.current() != PageManager::PAGE_AP_MODE) {
                Serial.println("[Main] Left AP page, stopping AP and reconnecting WiFi...");
                wifiManager.stopAPMode();
            }
        } else if (pageManager.current() == PageManager::PAGE_AP_MODE) {
            Serial.println("[Main] AP mode stopped, switching back to temp page...");
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pageManager.switchTo(PageManager::PAGE_TEMP);
                xSemaphoreGive(displayMutex);
            }
        } else if (wifiManager.isConnected()) {
            wifiManager.maintainConnection();
        } else {
            Serial.println("[Main] WiFi not connected, attempting connection...");
            if (wifiManager.connect()) {
                Serial.println("[Main] WiFi connection successful!");
                lastWeatherUpdate = now;
            }
        }
    }

    wifiManager.checkAPTimeout();
}

void handleTimeSync(unsigned long now) {
    if (!wifiManager.isConnected()) {
        return;
    }

    if (!timeSynced) {
        if (!firstSyncAttempted) {
            Serial.println("[Main] 首次时间同步尝试");
            if (timeManager.sync()) {
                timeSynced = true;
                lastTimeSync = now;
                Serial.println("[Main] 首次时间同步成功");
            } else {
                firstSyncAttempted = true;
                lastTimeSync = now;
                Serial.println("[Main] 首次时间同步失败，将每5分钟重试");
            }
        } else {
            if (now - lastTimeSync >= TIME_SYNC_INTERVAL_INITIAL) {
                lastTimeSync = now;
                Serial.println("[Main] 时间同步重试...");
                if (timeManager.sync()) {
                    timeSynced = true;
                    Serial.println("[Main] 时间同步成功");
                } else {
                    Serial.println("[Main] 时间同步失败，继续重试");
                }
            }
        }
    } else {
        if (now - lastTimeSync >= TIME_SYNC_INTERVAL_SUCCESS) {
            lastTimeSync = now;
            timeManager.sync();
        }
    }
}

void handleWeather(unsigned long now) {
    bool weatherDataEmpty = (weatherManager.getCity().length() == 0 ||
                            weatherManager.getTemperature().length() == 0 ||
                            weatherManager.getWeatherText().length() == 0);
    if (weatherDataEmpty || weatherStep != 0) {
        if (now < weatherBackoffEnd) {
            return;
        }
        if (wifiManager.isConnected()) {
            switch (weatherStep) {
                case 0:
                    Serial.println("[Main] Step 0/3: 通过IP获取定位");
                    if (weatherManager.fetchLocationByIP()) {
                        Serial.println("[Main] IP定位成功，继续获取城市信息");
                        weatherRetryCount = 0;
                    } else {
                        Serial.println("[Main] IP定位失败，使用默认位置");
                    }
                    weatherStep = 1;
                    break;
                case 1:
                    Serial.println("[Main] Step 1/3: 获取城市信息");
                    if (weatherManager.fetchCityInfo()) {
                        weatherStep = 2;
                        weatherRetryCount = 0;
                    } else {
                        weatherRetryCount++;
                        if (weatherRetryCount >= MAX_WEATHER_RETRIES) {
                            Serial.printf("[Main] Step 1 重试%d次失败，回退到step 0\n", MAX_WEATHER_RETRIES);
                            weatherStep = 0;
                            weatherRetryCount = 0;
                            weatherBackoffEnd = now + WEATHER_BACKOFF_MS;
                            Serial.printf("[Main] 天气获取退避5分钟\n");
                        }
                    }
                    break;
                case 2:
                    Serial.println("[Main] Step 2/3: 获取当前天气");
                    if (weatherManager.fetchCurrentWeather()) {
                        weatherStep = 3;
                        weatherRetryCount = 0;
                    } else {
                        weatherRetryCount++;
                        if (weatherRetryCount >= MAX_WEATHER_RETRIES) {
                            Serial.printf("[Main] Step 2 重试%d次失败，回退到step 0\n", MAX_WEATHER_RETRIES);
                            weatherStep = 0;
                            weatherRetryCount = 0;
                            weatherBackoffEnd = now + WEATHER_BACKOFF_MS;
                            Serial.printf("[Main] 天气获取退避5分钟\n");
                        }
                    }
                    break;
                case 3:
                    Serial.println("[Main] Step 3/3: 获取天气预报");
                    if (weatherManager.fetch3DayForecast()) {
                        weatherStep = 0;
                        lastWeatherUpdate = now;
                        weatherRetryCount = 0;
                        weatherBackoffEnd = 0;
                        Serial.println("[Main] 天气数据获取完成！");
                    } else {
                        weatherRetryCount++;
                        if (weatherRetryCount >= MAX_WEATHER_RETRIES) {
                            Serial.printf("[Main] Step 3 重试%d次失败，回退到step 0\n", MAX_WEATHER_RETRIES);
                            weatherStep = 0;
                            weatherRetryCount = 0;
                            weatherBackoffEnd = now + WEATHER_BACKOFF_MS;
                            Serial.printf("[Main] 天气获取退避5分钟\n");
                        }
                    }
                    break;
            }
        }
    } else if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
        lastWeatherUpdate = now;
        weatherStep = 1;
        weatherRetryCount = 0;
        weatherBackoffEnd = 0;
    }
}

void handleSensors(unsigned long now) {
    if (now - lastTempRead >= 5000) {
        lastTempRead = now;
        aht20Bmp280Sensor.update();
    }
}

void handleHistory(unsigned long now) {
    if (!timeSynced) {
        return;
    }

    if (lastHistorySave == 0) {
        if (timeManager.getYear() > 2020) {
            int currentMinute = timeManager.getHour() * 60 + timeManager.getMinute();
            int nextSlotMinute = ((currentMinute / 10) + 1) * 10;
            int delayToNextSlot = (nextSlotMinute - currentMinute) * 60000;
            lastHistorySave = millis() - (unsigned long)(600000 - delayToNextSlot);
            Serial.printf("[History] NTP同步成功，历史保存首次对齐到 %02d:%02d（%d 分钟后）\n",
                          (nextSlotMinute / 60) % 24, nextSlotMinute % 60,
                          delayToNextSlot / 60000);
        } else {
            lastHistorySave = millis();
        }
        return;
    }

    if (now - lastHistorySave >= 600000) {
        lastHistorySave = now;
        if (aht20Bmp280Sensor.isValid()) {
            pHistoryPage->addRecord(
                aht20Bmp280Sensor.getTemperature(),
                aht20Bmp280Sensor.getHumidity(),
                aht20Bmp280Sensor.getPressure()
            );
            Serial.printf("[History] 保存传感器数据: 温度=%.1f°C 湿度=%.1f%% 气压=%.1fhPa\n",
                aht20Bmp280Sensor.getTemperature(),
                aht20Bmp280Sensor.getHumidity(),
                aht20Bmp280Sensor.getPressure());

            if (pPressurePage->checkAlert()) {
                Serial.println("[Alert] 气压警告触发，自动切换到气压页面！");
                pageManager.switchTo(PageManager::PAGE_PRESSURE);
            }
        }
    }
}

void handleDisplay() {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pageManager.update();
        xSemaphoreGive(displayMutex); 
    }  
}

void handleLED(unsigned long now) {
    if (now - lastLEDConditionCheck >= 500) {
        lastLEDConditionCheck = now;
        if (!wifiManager.isConnected()) {
            ledController.setState(LED_STATE_BLINK_FAST);
        } else {
            ledController.setState(LED_STATE_OFF);
        }
    }
    ledController.update();
    buzzerController.update();
}

void handleChime() {
    static int lastChimeHour = -1;
    int currentHour = timeManager.getHour();
    int currentMinute = timeManager.getMinute();
    int currentSecond = timeManager.getSecond();
    
    if (currentHour >= 6 && currentHour <= 19 && 
        currentMinute == 59 && currentSecond == 55 && 
        currentHour != lastChimeHour) {
        lastChimeHour = currentHour;
        Serial.println("[Chime] 定时报时触发");
        buzzerController.radioChime();
    } else if (currentMinute != 59 || currentSecond != 55) {
        lastChimeHour = -1;
    }
}

void handleBrightness(unsigned long now) {
    if (now - lastAutoBrightnessCheck >= 60000) {
        lastAutoBrightnessCheck = now;
        if (wifiManager.isConnected()) {
            int targetLevel = calculateAutoBrightness();
            if (targetLevel >= 0 && targetLevel != currentBacklightLevel) {
                int oldLevel = currentBacklightLevel;
                currentBacklightLevel = targetLevel;
                setBacklightLevel(BACKLIGHT_LEVELS[currentBacklightLevel]);
                Serial.printf("[Brightness] Auto: %d -> %d\n", 
                    BACKLIGHT_LEVELS[oldLevel], BACKLIGHT_LEVELS[currentBacklightLevel]);
            }
        }
    }
    yield();
}
