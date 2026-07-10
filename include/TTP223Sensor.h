#ifndef TTP223_SENSOR_H
#define TTP223_SENSOR_H

#include <Arduino.h>

typedef enum {
    TOUCH_NONE,
    TOUCH_SHORT,
    TOUCH_LONG,
    TOUCH_VERY_LONG,
    TOUCH_DOUBLE
} TouchType;

#define LONG_PRESS_THRESHOLD 500
#define VERY_LONG_PRESS_THRESHOLD 10000  // 10秒 - 进入AP模式
#define DOUBLE_CLICK_TIMEOUT 800

class TTP223Sensor {
public:
    TTP223Sensor(int pin);
    void begin();
    void update();
    TouchType getLastTouchType() const;
    bool hasNewTouch() const;
    void clearTouchEvent();

private:
    int _pin;
    bool _touchDetected;
    bool _stableState;
    int _stableCount;
    const int _stableThreshold = 5;
    unsigned long _touchStartTime;
    unsigned long _lastTouchEndTime;
    int _clickCount;
    bool _longPressHandled;
    bool _veryLongPressHandled;
    TouchType _lastTouchType;
    bool _newTouchAvailable;
};

#endif
