#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "DisplayManager.h"
#include "TaskManager.h"

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
#include <driver/gpio.h>
#include <esp_log.h>

static const char* TAG = "Main";

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
PageManager          pageManager(displayManager);

// ==================== TaskManager ====================

TaskManager* taskManager = nullptr;

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

unsigned long lastLEDConditionCheck = 0;
unsigned long lastAutoBrightnessCheck = 0;
int lastAutoBrightnessDay = -1;
SemaphoreHandle_t displayMutex = NULL;



// ==================== 背光与触摸辅助函数 ====================

static void setBacklightLevel(int level) {
    // 先分离，再重新附加，避免 LEDC 通道卡住
    ledcDetachPin(PIN_TFT_BL);
    ledcAttachPin(PIN_TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, level);
}

// 按时段自动调光 (参考 screen_st7735 项目 taskBacklight)
// 时段→等级映射 (BACKLIGHT_LEVELS 共 4 档: 10/80/160/255):
//   06-08: level 1 (晨间, 80)
//   08-18: level 2 (白天, 160)
//   18-20: level 1 (傍晚, 80)
//   20-22: level 1 (夜间, 80)
//   其他:   level 0 (深夜, 10)
static int calculateAutoBrightness() {
    int h = timeManager.getHour();
    int target;
    if      (h >= 6  && h < 8)  target = 1;
    else if (h >= 8  && h < 18) target = 2;
    else if (h >= 18 && h < 22) target = 1;
    else                        target = 0;

    ESP_LOGI(TAG, "[Brightness] 当前小时:%d 等级:%d", h, target);
    return target;
}

static void handleTouchEvent(TouchType type) {
    switch (type) {
        case TOUCH_SHORT: {
            ESP_LOGI(TAG, "[Touch] Short touch - next page");
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
            ESP_LOGI(TAG, "[Touch] Double touch - prev page");
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
            ESP_LOGI(TAG, "[Touch] Very long touch - Enter AP mode");
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

      // ========== 3. 挂载 LittleFS ==========
    if (!LittleFS.begin(true)) {
        ESP_LOGE(TAG, "[LittleFS] Mount Failed - Formatting...");
        if (!LittleFS.begin(true)) {
            ESP_LOGE(TAG, "[LittleFS] Format Failed");
        } else {
            ESP_LOGI(TAG, "[LittleFS] Format Success");
        }
    } else {
        ESP_LOGI(TAG, "[LittleFS] Mount Success");
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
    ESP_LOGI(TAG, "[Backlight] 重新附加背光 PWM, 等级: %d", currentBacklightLevel);

 // ========== 7. 屏幕初始化后，重新附加蜂鸣器PWM ==========
    // TFT_eSPI.init() 可能重置了GPIO状态，所以重新附加
    // 注意：不需要再 pinMode + digitalWrite，因为 ledcAttachPin 会接管引脚
    ledcDetachPin(PIN_BUZZER);
    ledcAttachPin(PIN_BUZZER, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, (1 << LEDC_TIMER_BIT) - 1);
    ESP_LOGI(TAG, "蜂鸣器 PWM 重新附加，静音状态（高电平）");

    // AHT20+BMP280
    if (!aht20Bmp280Sensor.begin()) {
        ESP_LOGE(TAG, "AHT20+BMP280 传感器初始化失败");
    } else {
        ESP_LOGI(TAG, "AHT20+BMP280 传感器初始化成功");
        delay(100);
        aht20Bmp280Sensor.update();
    }


    // LED
    ledController.begin();

    // Buzzer
    buzzerController.begin();

    // 触摸
    touchSensor.begin();
    ESP_LOGI(TAG, "TTP223 touch sensor initialized");

    // 互斥锁
    displayMutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "\n================================================");
    ESP_LOGI(TAG, "   ESP32-C3 Weather Clock (Page-based)");
    ESP_LOGI(TAG, "================================================\n");

    wifiManager.connect();
    timeManager.update();

    if (wifiManager.isConnected()) {
        ESP_LOGI(TAG, "WiFi连接成功，开始时间同步");
        if (timeManager.sync()) {
            ESP_LOGI(TAG, "NTP时间同步成功");
        } else {
            ESP_LOGE(TAG, "NTP时间同步失败，将在任务中重试");
        }
    } else {
        ESP_LOGE(TAG, "WiFi连接失败，开启AP配网模式");
        wifiManager.startAPMode();
    }

    // 启动页面管理器
    pTempPage     = new TempPage(displayManager, weatherManager, aht20Bmp280Sensor, wifiManager);
    pCalendarPage = new CalendarPage(displayManager, timeManager);
    pForecastPage = new ForecastPage(displayManager, weatherManager, timeManager, wifiManager);
    pPressurePage = new PressurePage(displayManager, aht20Bmp280Sensor);
    pHistoryPage            = new HistoryPage(displayManager, aht20Bmp280Sensor);
    pAPModePage            = new APModePage(displayManager, wifiManager);
    pWiFiInfoPage          = new WiFiInfoPage(displayManager, wifiManager);
    pStreamingPlayerPage   = new StreamingPlayerPage(displayManager);

    pageManager.registerPage(PageManager::PAGE_TEMP,         pTempPage);
    pageManager.registerPage(PageManager::PAGE_FORECAST,     pForecastPage);
    pageManager.registerPage(PageManager::PAGE_CALENDAR,     pCalendarPage);
    pageManager.registerPage(PageManager::PAGE_PRESSURE,     pPressurePage);
    pageManager.registerPage(PageManager::PAGE_HISTORY,      pHistoryPage);
    pageManager.registerPage(PageManager::PAGE_WIFI_INFO,    pWiFiInfoPage);
    pageManager.registerPage(PageManager::PAGE_AP_MODE,      pAPModePage);
    pageManager.registerPage(PageManager::PAGE_STREAMING,    pStreamingPlayerPage);
    
    pageManager.begin();

    // 创建并启动 TaskManager
    taskManager = new TaskManager(
        wifiManager,
        timeManager,
        weatherManager,
        aht20Bmp280Sensor,
        pHistoryPage,
        pPressurePage,
        pTempPage,
        pageManager,
        displayMutex
    );
    taskManager->begin();
   
    // 播放启动自检声
    buzzerController.startupChime();
    
}

// ==================== loop 功能函数声明 ====================

void handleTouch();
void handleDisplay();
void handleLED(unsigned long now);
void handleChime();
void handleBrightness(unsigned long now);

// ==================== loop ====================

void loop() {
    unsigned long now = millis();
    handleTouch();
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
        ESP_LOGI(TAG, "[Chime] 定时报时触发");
        buzzerController.radioChime();
    } else if (currentMinute != 59 || currentSecond != 55) {
        lastChimeHour = -1;
    }
}

// 背光自动调光: NTP 未同步前固定 80% (PWM=204), 同步后每 60 秒按时间段分档
// 不依赖网络, 只需 NTP 同步过一次即可使用本地时间
void handleBrightness(unsigned long now) {
    if (now - lastAutoBrightnessCheck < 60000) {
        yield();
        return;
    }
    lastAutoBrightnessCheck = now;

    // NTP 未同步前维持 80% 亮度 (PWM = 80 * 255 / 100 ≈ 204)
    if (taskManager == nullptr || !taskManager->isTimeSynced()) {
        const int pwm80 = (80 * 255 + 50) / 100;
        if (currentBacklightLevel != -1) {
            currentBacklightLevel = -1;  // 标记为未同步默认态, 同步后必触发切换
            setBacklightLevel(pwm80);
            ESP_LOGW(TAG, "[Brightness] NTP 未同步, 默认 80%% (PWM=%d)", pwm80);
        }
        yield();
        return;
    }

    int targetLevel = calculateAutoBrightness();
    if (targetLevel != currentBacklightLevel) {
        int oldLevel = currentBacklightLevel;
        currentBacklightLevel = targetLevel;
        setBacklightLevel(BACKLIGHT_LEVELS[currentBacklightLevel]);
        ESP_LOGI(TAG, "[Brightness] Auto: %d -> %d",
            (oldLevel >= 0) ? BACKLIGHT_LEVELS[oldLevel] : -1,
            BACKLIGHT_LEVELS[currentBacklightLevel]);
    }
    yield();
}