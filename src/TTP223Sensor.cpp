#include "TTP223Sensor.h"
#include <esp_log.h>

static const char* TAG = "TTP223";

TTP223Sensor::TTP223Sensor(int pin) : _pin(pin) {
}

void TTP223Sensor::begin() {
    pinMode(_pin, INPUT_PULLUP);
    _touchDetected = false;
    _stableState = false;
    _stableCount = 0;
    _touchStartTime = 0;
    _lastTouchEndTime = 0;
    _clickCount = 0;
    _longPressHandled = false;
    _veryLongPressHandled = false;
    _lastTouchType = TOUCH_NONE;
    _newTouchAvailable = false;
    ESP_LOGI(TAG, "Sensor initialized on GPIO%d", _pin);
}

void TTP223Sensor::update() {
    bool currentState = digitalRead(_pin);
    
    if (currentState == _stableState) {
        _stableCount++;
    } else {
        _stableState = currentState;
        _stableCount = 1;
    }
    
    if (_stableCount >= _stableThreshold) {
        static bool lastReportedState = false;
        
        if (_stableState != lastReportedState) {
            lastReportedState = _stableState;
            
            if (_stableState == HIGH) {
                _touchDetected = true;
                _touchStartTime = millis();
                _longPressHandled = false;
                _veryLongPressHandled = false;
                
                unsigned long timeSinceLastTouch = millis() - _lastTouchEndTime;
                ESP_LOGI(TAG, "Touch pressed | timeSinceLast: %lu ms | clickCount: %d", timeSinceLastTouch, _clickCount);
                
                if (timeSinceLastTouch < DOUBLE_CLICK_TIMEOUT && _clickCount == 1) {
                    _clickCount = 0;
                    _lastTouchType = TOUCH_DOUBLE;
                    _newTouchAvailable = true;
                    ESP_LOGI(TAG, "=== DOUBLE CLICK DETECTED ===");
                } else {
                    _clickCount = 1;
                }
            } else {
                _touchDetected = false;
                _lastTouchEndTime = millis();
                
                unsigned long touchDuration = millis() - _touchStartTime;
                ESP_LOGI(TAG, "Touch released | duration: %lu ms", touchDuration);
                
                if (touchDuration >= LONG_PRESS_THRESHOLD) {
                    _lastTouchType = TOUCH_LONG;
                    _newTouchAvailable = true;
                    _clickCount = 0;
                    _longPressHandled = true;
                    ESP_LOGI(TAG, "=== LONG TOUCH DETECTED ===");
                }
            }
        }
    }
    
    if (_touchDetected && !_veryLongPressHandled) {
        unsigned long touchDuration = millis() - _touchStartTime;
        if (touchDuration >= VERY_LONG_PRESS_THRESHOLD) {
            _lastTouchType = TOUCH_VERY_LONG;
            _newTouchAvailable = true;
            _clickCount = 0;
            _longPressHandled = true;
            _veryLongPressHandled = true;
            ESP_LOGI(TAG, "=== VERY LONG TOUCH DETECTED (10s) ===");
        }
    }

    if (_touchDetected && !_longPressHandled && _veryLongPressHandled == false) {
        // 注意：如果已经判定为 very long，跳过普通 long 事件
    }

    if (_touchDetected && !_longPressHandled) {
        unsigned long touchDuration = millis() - _touchStartTime;
        if (touchDuration >= LONG_PRESS_THRESHOLD) {
            _lastTouchType = TOUCH_LONG;
            _newTouchAvailable = true;
            _clickCount = 0;
            _longPressHandled = true;
        }
    }
    
    if (!_touchDetected && _clickCount == 1) {
        unsigned long timeSinceLastTouch = millis() - _lastTouchEndTime;
        if (timeSinceLastTouch >= DOUBLE_CLICK_TIMEOUT) {
            _lastTouchType = TOUCH_SHORT;
            _newTouchAvailable = true;
            _clickCount = 0;
            ESP_LOGI(TAG, "=== SHORT TOUCH DETECTED ===");
        }
    }
}

TouchType TTP223Sensor::getLastTouchType() const {
    return _lastTouchType;
}

bool TTP223Sensor::hasNewTouch() const {
    return _newTouchAvailable;
}

void TTP223Sensor::clearTouchEvent() {
    _newTouchAvailable = false;
    _lastTouchType = TOUCH_NONE;
}
